/*	$OpenBSD: if_sk.c,v 2.33 2003/08/12 05:23:06 nate Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * SysKonnect SK-NET gigabit ethernet driver for FreeBSD. Supports
 * the SK-984x series adapters, both single port and dual port.
 * References:
 * 	The XaQti XMAC II datasheet,
 *  https://www.freebsd.org/~wpaul/SysKonnect/xmacii_datasheet_rev_c_9-29.pdf
 *	The SysKonnect GEnesis manual, http://www.syskonnect.com
 *
 * Note: XaQti has been acquired by Vitesse, and Vitesse does not have the
 * XMAC II datasheet online. I have put my copy at people.freebsd.org as a
 * convenience to others until Vitesse corrects this problem:
 *
 * https://people.freebsd.org/~wpaul/SysKonnect/xmacii_datasheet_rev_c_9-29.pdf
 *
 * Written by Bill Paul <wpaul@ee.columbia.edu>
 * Department of Electrical Engineering
 * Columbia University, New York City
 */
/*
 * The SysKonnect gigabit ethernet adapters consist of two main
 * components: the SysKonnect GEnesis controller chip and the XaQti Corp.
 * XMAC II gigabit ethernet MAC. The XMAC provides all of the MAC
 * components and a PHY while the GEnesis controller provides a PCI
 * interface with DMA support. Each card may have between 512K and
 * 2MB of SRAM on board depending on the configuration.
 *
 * The SysKonnect GEnesis controller can have either one or two XMAC
 * chips connected to it, allowing single or dual port NIC configurations.
 * SysKonnect has the distinction of being the only vendor on the market
 * with a dual port gigabit ethernet NIC. The GEnesis provides dual FIFOs,
 * dual DMA queues, packet/MAC/transmit arbiters and direct access to the
 * XMAC registers. This driver takes advantage of these features to allow
 * both XMACs to operate as independent interfaces.
 */

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

#include <machine/bus.h>
#include <machine/in_cksum.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/brgphyreg.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#if 0
#define SK_USEIOSPACE
#endif

#include <dev/sk/if_skreg.h>
#include <dev/sk/xmaciireg.h>
#include <dev/sk/yukonreg.h>

MODULE_DEPEND(sk, pci, 1, 1, 1);
MODULE_DEPEND(sk, ether, 1, 1, 1);
MODULE_DEPEND(sk, miibus, 1, 1, 1);

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

static const struct sk_type sk_devs[] = {
	{
		VENDORID_SK,
		DEVICEID_SK_V1,
		"SysKonnect Gigabit Ethernet (V1.0)"
	},
	{
		VENDORID_SK,
		DEVICEID_SK_V2,
		"SysKonnect Gigabit Ethernet (V2.0)"
	},
	{
		VENDORID_MARVELL,
		DEVICEID_SK_V2,
		"Marvell Gigabit Ethernet"
	},
	{
		VENDORID_MARVELL,
		DEVICEID_BELKIN_5005,
		"Belkin F5D5005 Gigabit Ethernet"
	},
	{
		VENDORID_3COM,
		DEVICEID_3COM_3C940,
		"3Com 3C940 Gigabit Ethernet"
	},
	{
		VENDORID_LINKSYS,
		DEVICEID_LINKSYS_EG1032,
		"Linksys EG1032 Gigabit Ethernet"
	},
	{
		VENDORID_DLINK,
		DEVICEID_DLINK_DGE530T_A1,
		"D-Link DGE-530T Gigabit Ethernet"
	},
	{
		VENDORID_DLINK,
		DEVICEID_DLINK_DGE530T_B1,
		"D-Link DGE-530T Gigabit Ethernet"
	},
	{ 0, 0, NULL }
};

static int skc_probe(device_t);
static int skc_attach(device_t);
static int skc_detach(device_t);
static int skc_shutdown(device_t);
static int skc_suspend(device_t);
static int skc_resume(device_t);
static bus_dma_tag_t skc_get_dma_tag(device_t, device_t);
static int sk_detach(device_t);
static int sk_probe(device_t);
static int sk_attach(device_t);
static void sk_tick(void *);
static void sk_yukon_tick(void *);
static void sk_intr(void *);
static void sk_intr_xmac(struct sk_if_softc *);
static void sk_intr_bcom(struct sk_if_softc *);
static void sk_intr_yukon(struct sk_if_softc *);
static __inline void sk_rxcksum(struct ifnet *, struct mbuf *, u_int32_t);
static __inline int sk_rxvalid(struct sk_softc *, u_int32_t, u_int32_t);
static void sk_rxeof(struct sk_if_softc *);
static void sk_jumbo_rxeof(struct sk_if_softc *);
static void sk_txeof(struct sk_if_softc *);
static void sk_txcksum(struct ifnet *, struct mbuf *, struct sk_tx_desc *);
static int sk_encap(struct sk_if_softc *, struct mbuf **);
static void sk_start(struct ifnet *);
static void sk_start_locked(struct ifnet *);
static int sk_ioctl(struct ifnet *, u_long, caddr_t);
static void sk_init(void *);
static void sk_init_locked(struct sk_if_softc *);
static void sk_init_xmac(struct sk_if_softc *);
static void sk_init_yukon(struct sk_if_softc *);
static void sk_stop(struct sk_if_softc *);
static void sk_watchdog(void *);
static int sk_ifmedia_upd(struct ifnet *);
static void sk_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static void sk_reset(struct sk_softc *);
static __inline void sk_discard_rxbuf(struct sk_if_softc *, int);
static __inline void sk_discard_jumbo_rxbuf(struct sk_if_softc *, int);
static int sk_newbuf(struct sk_if_softc *, int);
static int sk_jumbo_newbuf(struct sk_if_softc *, int);
static void sk_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static int sk_dma_alloc(struct sk_if_softc *);
static int sk_dma_jumbo_alloc(struct sk_if_softc *);
static void sk_dma_free(struct sk_if_softc *);
static void sk_dma_jumbo_free(struct sk_if_softc *);
static int sk_init_rx_ring(struct sk_if_softc *);
static int sk_init_jumbo_rx_ring(struct sk_if_softc *);
static void sk_init_tx_ring(struct sk_if_softc *);
static u_int32_t sk_win_read_4(struct sk_softc *, int);
static u_int16_t sk_win_read_2(struct sk_softc *, int);
static u_int8_t sk_win_read_1(struct sk_softc *, int);
static void sk_win_write_4(struct sk_softc *, int, u_int32_t);
static void sk_win_write_2(struct sk_softc *, int, u_int32_t);
static void sk_win_write_1(struct sk_softc *, int, u_int32_t);

static int sk_miibus_readreg(device_t, int, int);
static int sk_miibus_writereg(device_t, int, int, int);
static void sk_miibus_statchg(device_t);

static int sk_xmac_miibus_readreg(struct sk_if_softc *, int, int);
static int sk_xmac_miibus_writereg(struct sk_if_softc *, int, int,
						int);
static void sk_xmac_miibus_statchg(struct sk_if_softc *);

static int sk_marv_miibus_readreg(struct sk_if_softc *, int, int);
static int sk_marv_miibus_writereg(struct sk_if_softc *, int, int,
						int);
static void sk_marv_miibus_statchg(struct sk_if_softc *);

static uint32_t sk_xmchash(const uint8_t *);
static void sk_setfilt(struct sk_if_softc *, u_int16_t *, int);
static void sk_rxfilter(struct sk_if_softc *);
static void sk_rxfilter_genesis(struct sk_if_softc *);
static void sk_rxfilter_yukon(struct sk_if_softc *);

static int sysctl_int_range(SYSCTL_HANDLER_ARGS, int low, int high);
static int sysctl_hw_sk_int_mod(SYSCTL_HANDLER_ARGS);

/* Tunables. */
static int jumbo_disable = 0;
TUNABLE_INT("hw.skc.jumbo_disable", &jumbo_disable);
 
/*
 * It seems that SK-NET GENESIS supports very simple checksum offload
 * capability for Tx and I believe it can generate 0 checksum value for
 * UDP packets in Tx as the hardware can't differenciate UDP packets from
 * TCP packets. 0 chcecksum value for UDP packet is an invalid one as it
 * means sender didn't perforam checksum computation. For the safety I
 * disabled UDP checksum offload capability at the moment. Alternatively
 * we can intrduce a LINK0/LINK1 flag as hme(4) did in its Tx checksum
 * offload routine.
 */
#define SK_CSUM_FEATURES	(CSUM_TCP)

/*
 * Note that we have newbus methods for both the GEnesis controller
 * itself and the XMAC(s). The XMACs are children of the GEnesis, and
 * the miibus code is a child of the XMACs. We need to do it this way
 * so that the miibus drivers can access the PHY registers on the
 * right PHY. It's not quite what I had in mind, but it's the only
 * design that achieves the desired effect.
 */
static device_method_t skc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		skc_probe),
	DEVMETHOD(device_attach,	skc_attach),
	DEVMETHOD(device_detach,	skc_detach),
	DEVMETHOD(device_suspend,	skc_suspend),
	DEVMETHOD(device_resume,	skc_resume),
	DEVMETHOD(device_shutdown,	skc_shutdown),

	DEVMETHOD(bus_get_dma_tag,	skc_get_dma_tag),

	DEVMETHOD_END
};

static driver_t skc_driver = {
	"skc",
	skc_methods,
	sizeof(struct sk_softc)
};

static devclass_t skc_devclass;

static device_method_t sk_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sk_probe),
	DEVMETHOD(device_attach,	sk_attach),
	DEVMETHOD(device_detach,	sk_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	sk_miibus_readreg),
	DEVMETHOD(miibus_writereg,	sk_miibus_writereg),
	DEVMETHOD(miibus_statchg,	sk_miibus_statchg),

	DEVMETHOD_END
};

static driver_t sk_driver = {
	"sk",
	sk_methods,
	sizeof(struct sk_if_softc)
};

static devclass_t sk_devclass;

DRIVER_MODULE(skc, pci, skc_driver, skc_devclass, NULL, NULL);
DRIVER_MODULE(sk, skc, sk_driver, sk_devclass, NULL, NULL);
DRIVER_MODULE(miibus, sk, miibus_driver, miibus_devclass, NULL, NULL);

static struct resource_spec sk_res_spec_io[] = {
	{ SYS_RES_IOPORT,	PCIR_BAR(1),	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE },
	{ -1,			0,		0 }
};

static struct resource_spec sk_res_spec_mem[] = {
	{ SYS_RES_MEMORY,	PCIR_BAR(0),	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE },
	{ -1,			0,		0 }
};

#define SK_SETBIT(sc, reg, x)		\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) | x)

#define SK_CLRBIT(sc, reg, x)		\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) & ~x)

#define SK_WIN_SETBIT_4(sc, reg, x)	\
	sk_win_write_4(sc, reg, sk_win_read_4(sc, reg) | x)

#define SK_WIN_CLRBIT_4(sc, reg, x)	\
	sk_win_write_4(sc, reg, sk_win_read_4(sc, reg) & ~x)

#define SK_WIN_SETBIT_2(sc, reg, x)	\
	sk_win_write_2(sc, reg, sk_win_read_2(sc, reg) | x)

#define SK_WIN_CLRBIT_2(sc, reg, x)	\
	sk_win_write_2(sc, reg, sk_win_read_2(sc, reg) & ~x)

static u_int32_t
sk_win_read_4(sc, reg)
	struct sk_softc		*sc;
	int			reg;
{
#ifdef SK_USEIOSPACE
	CSR_WRITE_4(sc, SK_RAP, SK_WIN(reg));
	return(CSR_READ_4(sc, SK_WIN_BASE + SK_REG(reg)));
#else
	return(CSR_READ_4(sc, reg));
#endif
}

static u_int16_t
sk_win_read_2(sc, reg)
	struct sk_softc		*sc;
	int			reg;
{
#ifdef SK_USEIOSPACE
	CSR_WRITE_4(sc, SK_RAP, SK_WIN(reg));
	return(CSR_READ_2(sc, SK_WIN_BASE + SK_REG(reg)));
#else
	return(CSR_READ_2(sc, reg));
#endif
}

static u_int8_t
sk_win_read_1(sc, reg)
	struct sk_softc		*sc;
	int			reg;
{
#ifdef SK_USEIOSPACE
	CSR_WRITE_4(sc, SK_RAP, SK_WIN(reg));
	return(CSR_READ_1(sc, SK_WIN_BASE + SK_REG(reg)));
#else
	return(CSR_READ_1(sc, reg));
#endif
}

static void
sk_win_write_4(sc, reg, val)
	struct sk_softc		*sc;
	int			reg;
	u_int32_t		val;
{
#ifdef SK_USEIOSPACE
	CSR_WRITE_4(sc, SK_RAP, SK_WIN(reg));
	CSR_WRITE_4(sc, SK_WIN_BASE + SK_REG(reg), val);
#else
	CSR_WRITE_4(sc, reg, val);
#endif
	return;
}

static void
sk_win_write_2(sc, reg, val)
	struct sk_softc		*sc;
	int			reg;
	u_int32_t		val;
{
#ifdef SK_USEIOSPACE
	CSR_WRITE_4(sc, SK_RAP, SK_WIN(reg));
	CSR_WRITE_2(sc, SK_WIN_BASE + SK_REG(reg), val);
#else
	CSR_WRITE_2(sc, reg, val);
#endif
	return;
}

static void
sk_win_write_1(sc, reg, val)
	struct sk_softc		*sc;
	int			reg;
	u_int32_t		val;
{
#ifdef SK_USEIOSPACE
	CSR_WRITE_4(sc, SK_RAP, SK_WIN(reg));
	CSR_WRITE_1(sc, SK_WIN_BASE + SK_REG(reg), val);
#else
	CSR_WRITE_1(sc, reg, val);
#endif
	return;
}

static int
sk_miibus_readreg(dev, phy, reg)
	device_t		dev;
	int			phy, reg;
{
	struct sk_if_softc	*sc_if;
	int			v;

	sc_if = device_get_softc(dev);

	SK_IF_MII_LOCK(sc_if);
	switch(sc_if->sk_softc->sk_type) {
	case SK_GENESIS:
		v = sk_xmac_miibus_readreg(sc_if, phy, reg);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		v = sk_marv_miibus_readreg(sc_if, phy, reg);
		break;
	default:
		v = 0;
		break;
	}
	SK_IF_MII_UNLOCK(sc_if);

	return (v);
}

static int
sk_miibus_writereg(dev, phy, reg, val)
	device_t		dev;
	int			phy, reg, val;
{
	struct sk_if_softc	*sc_if;
	int			v;

	sc_if = device_get_softc(dev);

	SK_IF_MII_LOCK(sc_if);
	switch(sc_if->sk_softc->sk_type) {
	case SK_GENESIS:
		v = sk_xmac_miibus_writereg(sc_if, phy, reg, val);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		v = sk_marv_miibus_writereg(sc_if, phy, reg, val);
		break;
	default:
		v = 0;
		break;
	}
	SK_IF_MII_UNLOCK(sc_if);

	return (v);
}

static void
sk_miibus_statchg(dev)
	device_t		dev;
{
	struct sk_if_softc	*sc_if;

	sc_if = device_get_softc(dev);

	SK_IF_MII_LOCK(sc_if);
	switch(sc_if->sk_softc->sk_type) {
	case SK_GENESIS:
		sk_xmac_miibus_statchg(sc_if);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		sk_marv_miibus_statchg(sc_if);
		break;
	}
	SK_IF_MII_UNLOCK(sc_if);

	return;
}

static int
sk_xmac_miibus_readreg(sc_if, phy, reg)
	struct sk_if_softc	*sc_if;
	int			phy, reg;
{
	int			i;

	SK_XM_WRITE_2(sc_if, XM_PHY_ADDR, reg|(phy << 8));
	SK_XM_READ_2(sc_if, XM_PHY_DATA);
	if (sc_if->sk_phytype != SK_PHYTYPE_XMAC) {
		for (i = 0; i < SK_TIMEOUT; i++) {
			DELAY(1);
			if (SK_XM_READ_2(sc_if, XM_MMUCMD) &
			    XM_MMUCMD_PHYDATARDY)
				break;
		}

		if (i == SK_TIMEOUT) {
			if_printf(sc_if->sk_ifp, "phy failed to come ready\n");
			return(0);
		}
	}
	DELAY(1);
	i = SK_XM_READ_2(sc_if, XM_PHY_DATA);

	return(i);
}

static int
sk_xmac_miibus_writereg(sc_if, phy, reg, val)
	struct sk_if_softc	*sc_if;
	int			phy, reg, val;
{
	int			i;

	SK_XM_WRITE_2(sc_if, XM_PHY_ADDR, reg|(phy << 8));
	for (i = 0; i < SK_TIMEOUT; i++) {
		if (!(SK_XM_READ_2(sc_if, XM_MMUCMD) & XM_MMUCMD_PHYBUSY))
			break;
	}

	if (i == SK_TIMEOUT) {
		if_printf(sc_if->sk_ifp, "phy failed to come ready\n");
		return (ETIMEDOUT);
	}

	SK_XM_WRITE_2(sc_if, XM_PHY_DATA, val);
	for (i = 0; i < SK_TIMEOUT; i++) {
		DELAY(1);
		if (!(SK_XM_READ_2(sc_if, XM_MMUCMD) & XM_MMUCMD_PHYBUSY))
			break;
	}
	if (i == SK_TIMEOUT)
		if_printf(sc_if->sk_ifp, "phy write timed out\n");

	return(0);
}

static void
sk_xmac_miibus_statchg(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct mii_data		*mii;

	mii = device_get_softc(sc_if->sk_miibus);

	/*
	 * If this is a GMII PHY, manually set the XMAC's
	 * duplex mode accordingly.
	 */
	if (sc_if->sk_phytype != SK_PHYTYPE_XMAC) {
		if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
			SK_XM_SETBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_GMIIFDX);
		} else {
			SK_XM_CLRBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_GMIIFDX);
		}
	}
}

static int
sk_marv_miibus_readreg(sc_if, phy, reg)
	struct sk_if_softc	*sc_if;
	int			phy, reg;
{
	u_int16_t		val;
	int			i;

	if (sc_if->sk_phytype != SK_PHYTYPE_MARV_COPPER &&
	    sc_if->sk_phytype != SK_PHYTYPE_MARV_FIBER) {
		return(0);
	}

        SK_YU_WRITE_2(sc_if, YUKON_SMICR, YU_SMICR_PHYAD(phy) |
		      YU_SMICR_REGAD(reg) | YU_SMICR_OP_READ);

	for (i = 0; i < SK_TIMEOUT; i++) {
		DELAY(1);
		val = SK_YU_READ_2(sc_if, YUKON_SMICR);
		if (val & YU_SMICR_READ_VALID)
			break;
	}

	if (i == SK_TIMEOUT) {
		if_printf(sc_if->sk_ifp, "phy failed to come ready\n");
		return(0);
	}

	val = SK_YU_READ_2(sc_if, YUKON_SMIDR);

	return(val);
}

static int
sk_marv_miibus_writereg(sc_if, phy, reg, val)
	struct sk_if_softc	*sc_if;
	int			phy, reg, val;
{
	int			i;

	SK_YU_WRITE_2(sc_if, YUKON_SMIDR, val);
	SK_YU_WRITE_2(sc_if, YUKON_SMICR, YU_SMICR_PHYAD(phy) |
		      YU_SMICR_REGAD(reg) | YU_SMICR_OP_WRITE);

	for (i = 0; i < SK_TIMEOUT; i++) {
		DELAY(1);
		if ((SK_YU_READ_2(sc_if, YUKON_SMICR) & YU_SMICR_BUSY) == 0)
			break;
	}
	if (i == SK_TIMEOUT)
		if_printf(sc_if->sk_ifp, "phy write timeout\n");

	return(0);
}

static void
sk_marv_miibus_statchg(sc_if)
	struct sk_if_softc	*sc_if;
{
	return;
}

#define HASH_BITS		6

static u_int32_t
sk_xmchash(addr)
	const uint8_t *addr;
{
	uint32_t crc;

	/* Compute CRC for the address value. */
	crc = ether_crc32_le(addr, ETHER_ADDR_LEN);

	return (~crc & ((1 << HASH_BITS) - 1));
}

static void
sk_setfilt(sc_if, addr, slot)
	struct sk_if_softc	*sc_if;
	u_int16_t		*addr;
	int			slot;
{
	int			base;

	base = XM_RXFILT_ENTRY(slot);

	SK_XM_WRITE_2(sc_if, base, addr[0]);
	SK_XM_WRITE_2(sc_if, base + 2, addr[1]);
	SK_XM_WRITE_2(sc_if, base + 4, addr[2]);

	return;
}

static void
sk_rxfilter(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_softc		*sc;

	SK_IF_LOCK_ASSERT(sc_if);

	sc = sc_if->sk_softc;
	if (sc->sk_type == SK_GENESIS)
		sk_rxfilter_genesis(sc_if);
	else
		sk_rxfilter_yukon(sc_if);
}

static void
sk_rxfilter_genesis(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct ifnet		*ifp = sc_if->sk_ifp;
	u_int32_t		hashes[2] = { 0, 0 }, mode;
	int			h = 0, i;
	struct ifmultiaddr	*ifma;
	u_int16_t		dummy[] = { 0, 0, 0 };
	u_int16_t		maddr[(ETHER_ADDR_LEN+1)/2];

	SK_IF_LOCK_ASSERT(sc_if);

	mode = SK_XM_READ_4(sc_if, XM_MODE);
	mode &= ~(XM_MODE_RX_PROMISC | XM_MODE_RX_USE_HASH |
	    XM_MODE_RX_USE_PERFECT);
	/* First, zot all the existing perfect filters. */
	for (i = 1; i < XM_RXFILT_MAX; i++)
		sk_setfilt(sc_if, dummy, i);

	/* Now program new ones. */
	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		if (ifp->if_flags & IFF_ALLMULTI)
			mode |= XM_MODE_RX_USE_HASH;
		if (ifp->if_flags & IFF_PROMISC)
			mode |= XM_MODE_RX_PROMISC;
		hashes[0] = 0xFFFFFFFF;
		hashes[1] = 0xFFFFFFFF;
	} else {
		i = 1;
		if_maddr_rlock(ifp);
		/* XXX want to maintain reverse semantics */
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs,
		    ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			/*
			 * Program the first XM_RXFILT_MAX multicast groups
			 * into the perfect filter.
			 */
			bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
			    maddr, ETHER_ADDR_LEN);
			if (i < XM_RXFILT_MAX) {
				sk_setfilt(sc_if, maddr, i);
				mode |= XM_MODE_RX_USE_PERFECT;
				i++;
				continue;
			}
			h = sk_xmchash((const uint8_t *)maddr);
			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));
			mode |= XM_MODE_RX_USE_HASH;
		}
		if_maddr_runlock(ifp);
	}

	SK_XM_WRITE_4(sc_if, XM_MODE, mode);
	SK_XM_WRITE_4(sc_if, XM_MAR0, hashes[0]);
	SK_XM_WRITE_4(sc_if, XM_MAR2, hashes[1]);
}

static void
sk_rxfilter_yukon(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct ifnet		*ifp;
	u_int32_t		crc, hashes[2] = { 0, 0 }, mode;
	struct ifmultiaddr	*ifma;

	SK_IF_LOCK_ASSERT(sc_if);

	ifp = sc_if->sk_ifp;
	mode = SK_YU_READ_2(sc_if, YUKON_RCR);
	if (ifp->if_flags & IFF_PROMISC)
		mode &= ~(YU_RCR_UFLEN | YU_RCR_MUFLEN); 
	else if (ifp->if_flags & IFF_ALLMULTI) {
		mode |= YU_RCR_UFLEN | YU_RCR_MUFLEN; 
		hashes[0] = 0xFFFFFFFF;
		hashes[1] = 0xFFFFFFFF;
	} else {
		mode |= YU_RCR_UFLEN;
		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			crc = ether_crc32_be(LLADDR((struct sockaddr_dl *)
			    ifma->ifma_addr), ETHER_ADDR_LEN);
			/* Just want the 6 least significant bits. */
			crc &= 0x3f;
			/* Set the corresponding bit in the hash table. */
			hashes[crc >> 5] |= 1 << (crc & 0x1f);
		}
		if_maddr_runlock(ifp);
		if (hashes[0] != 0 || hashes[1] != 0)
			mode |= YU_RCR_MUFLEN;
	}

	SK_YU_WRITE_2(sc_if, YUKON_MCAH1, hashes[0] & 0xffff);
	SK_YU_WRITE_2(sc_if, YUKON_MCAH2, (hashes[0] >> 16) & 0xffff);
	SK_YU_WRITE_2(sc_if, YUKON_MCAH3, hashes[1] & 0xffff);
	SK_YU_WRITE_2(sc_if, YUKON_MCAH4, (hashes[1] >> 16) & 0xffff);
	SK_YU_WRITE_2(sc_if, YUKON_RCR, mode);
}

static int
sk_init_rx_ring(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_ring_data	*rd;
	bus_addr_t		addr;
	u_int32_t		csum_start;
	int			i;

	sc_if->sk_cdata.sk_rx_cons = 0;

	csum_start = (ETHER_HDR_LEN + sizeof(struct ip))  << 16 |
	    ETHER_HDR_LEN;
	rd = &sc_if->sk_rdata;
	bzero(rd->sk_rx_ring, sizeof(struct sk_rx_desc) * SK_RX_RING_CNT);
	for (i = 0; i < SK_RX_RING_CNT; i++) {
		if (sk_newbuf(sc_if, i) != 0)
			return (ENOBUFS);
		if (i == (SK_RX_RING_CNT - 1))
			addr = SK_RX_RING_ADDR(sc_if, 0);
		else
			addr = SK_RX_RING_ADDR(sc_if, i + 1);
		rd->sk_rx_ring[i].sk_next = htole32(SK_ADDR_LO(addr));
		rd->sk_rx_ring[i].sk_csum_start = htole32(csum_start);
	}

	bus_dmamap_sync(sc_if->sk_cdata.sk_rx_ring_tag,
	    sc_if->sk_cdata.sk_rx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return(0);
}

static int
sk_init_jumbo_rx_ring(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_ring_data	*rd;
	bus_addr_t		addr;
	u_int32_t		csum_start;
	int			i;

	sc_if->sk_cdata.sk_jumbo_rx_cons = 0;

	csum_start = ((ETHER_HDR_LEN + sizeof(struct ip)) << 16) |
	    ETHER_HDR_LEN;
	rd = &sc_if->sk_rdata;
	bzero(rd->sk_jumbo_rx_ring,
	    sizeof(struct sk_rx_desc) * SK_JUMBO_RX_RING_CNT);
	for (i = 0; i < SK_JUMBO_RX_RING_CNT; i++) {
		if (sk_jumbo_newbuf(sc_if, i) != 0)
			return (ENOBUFS);
		if (i == (SK_JUMBO_RX_RING_CNT - 1))
			addr = SK_JUMBO_RX_RING_ADDR(sc_if, 0);
		else
			addr = SK_JUMBO_RX_RING_ADDR(sc_if, i + 1);
		rd->sk_jumbo_rx_ring[i].sk_next = htole32(SK_ADDR_LO(addr));
		rd->sk_jumbo_rx_ring[i].sk_csum_start = htole32(csum_start);
	}

	bus_dmamap_sync(sc_if->sk_cdata.sk_jumbo_rx_ring_tag,
	    sc_if->sk_cdata.sk_jumbo_rx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

static void
sk_init_tx_ring(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_ring_data	*rd;
	struct sk_txdesc	*txd;
	bus_addr_t		addr;
	int			i;

	STAILQ_INIT(&sc_if->sk_cdata.sk_txfreeq);
	STAILQ_INIT(&sc_if->sk_cdata.sk_txbusyq);

	sc_if->sk_cdata.sk_tx_prod = 0;
	sc_if->sk_cdata.sk_tx_cons = 0;
	sc_if->sk_cdata.sk_tx_cnt = 0;

	rd = &sc_if->sk_rdata;
	bzero(rd->sk_tx_ring, sizeof(struct sk_tx_desc) * SK_TX_RING_CNT);
	for (i = 0; i < SK_TX_RING_CNT; i++) {
		if (i == (SK_TX_RING_CNT - 1))
			addr = SK_TX_RING_ADDR(sc_if, 0);
		else
			addr = SK_TX_RING_ADDR(sc_if, i + 1);
		rd->sk_tx_ring[i].sk_next = htole32(SK_ADDR_LO(addr));
		txd = &sc_if->sk_cdata.sk_txdesc[i];
		STAILQ_INSERT_TAIL(&sc_if->sk_cdata.sk_txfreeq, txd, tx_q);
	}

	bus_dmamap_sync(sc_if->sk_cdata.sk_tx_ring_tag,
	    sc_if->sk_cdata.sk_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static __inline void
sk_discard_rxbuf(sc_if, idx)
	struct sk_if_softc	*sc_if;
	int			idx;
{
	struct sk_rx_desc	*r;
	struct sk_rxdesc	*rxd;
	struct mbuf		*m;


	r = &sc_if->sk_rdata.sk_rx_ring[idx];
	rxd = &sc_if->sk_cdata.sk_rxdesc[idx];
	m = rxd->rx_m;
	r->sk_ctl = htole32(m->m_len | SK_RXSTAT | SK_OPCODE_CSUM);
}

static __inline void
sk_discard_jumbo_rxbuf(sc_if, idx)
	struct sk_if_softc	*sc_if;
	int			idx;
{
	struct sk_rx_desc	*r;
	struct sk_rxdesc	*rxd;
	struct mbuf		*m;

	r = &sc_if->sk_rdata.sk_jumbo_rx_ring[idx];
	rxd = &sc_if->sk_cdata.sk_jumbo_rxdesc[idx];
	m = rxd->rx_m;
	r->sk_ctl = htole32(m->m_len | SK_RXSTAT | SK_OPCODE_CSUM);
}

static int
sk_newbuf(sc_if, idx)
	struct sk_if_softc	*sc_if;
	int 			idx;
{
	struct sk_rx_desc	*r;
	struct sk_rxdesc	*rxd;
	struct mbuf		*m;
	bus_dma_segment_t	segs[1];
	bus_dmamap_t		map;
	int			nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, ETHER_ALIGN);

	if (bus_dmamap_load_mbuf_sg(sc_if->sk_cdata.sk_rx_tag,
	    sc_if->sk_cdata.sk_rx_sparemap, m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	rxd = &sc_if->sk_cdata.sk_rxdesc[idx];
	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc_if->sk_cdata.sk_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc_if->sk_cdata.sk_rx_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc_if->sk_cdata.sk_rx_sparemap;
	sc_if->sk_cdata.sk_rx_sparemap = map;
	bus_dmamap_sync(sc_if->sk_cdata.sk_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;
	r = &sc_if->sk_rdata.sk_rx_ring[idx];
	r->sk_data_lo = htole32(SK_ADDR_LO(segs[0].ds_addr));
	r->sk_data_hi = htole32(SK_ADDR_HI(segs[0].ds_addr));
	r->sk_ctl = htole32(segs[0].ds_len | SK_RXSTAT | SK_OPCODE_CSUM);

	return (0);
}

static int
sk_jumbo_newbuf(sc_if, idx)
	struct sk_if_softc	*sc_if;
	int			idx;
{
	struct sk_rx_desc	*r;
	struct sk_rxdesc	*rxd;
	struct mbuf		*m;
	bus_dma_segment_t	segs[1];
	bus_dmamap_t		map;
	int			nsegs;

	m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MJUM9BYTES);
	if (m == NULL)
		return (ENOBUFS);
	m->m_pkthdr.len = m->m_len = MJUM9BYTES;
	/*
	 * Adjust alignment so packet payload begins on a
	 * longword boundary. Mandatory for Alpha, useful on
	 * x86 too.
	 */
	m_adj(m, ETHER_ALIGN);

	if (bus_dmamap_load_mbuf_sg(sc_if->sk_cdata.sk_jumbo_rx_tag,
	    sc_if->sk_cdata.sk_jumbo_rx_sparemap, m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	rxd = &sc_if->sk_cdata.sk_jumbo_rxdesc[idx];
	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc_if->sk_cdata.sk_jumbo_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc_if->sk_cdata.sk_jumbo_rx_tag,
		    rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc_if->sk_cdata.sk_jumbo_rx_sparemap;
	sc_if->sk_cdata.sk_jumbo_rx_sparemap = map;
	bus_dmamap_sync(sc_if->sk_cdata.sk_jumbo_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;
	r = &sc_if->sk_rdata.sk_jumbo_rx_ring[idx];
	r->sk_data_lo = htole32(SK_ADDR_LO(segs[0].ds_addr));
	r->sk_data_hi = htole32(SK_ADDR_HI(segs[0].ds_addr));
	r->sk_ctl = htole32(segs[0].ds_len | SK_RXSTAT | SK_OPCODE_CSUM);

	return (0);
}

/*
 * Set media options.
 */
static int
sk_ifmedia_upd(ifp)
	struct ifnet		*ifp;
{
	struct sk_if_softc	*sc_if = ifp->if_softc;
	struct mii_data		*mii;

	mii = device_get_softc(sc_if->sk_miibus);
	sk_init(sc_if);
	mii_mediachg(mii);

	return(0);
}

/*
 * Report current media status.
 */
static void
sk_ifmedia_sts(ifp, ifmr)
	struct ifnet		*ifp;
	struct ifmediareq	*ifmr;
{
	struct sk_if_softc	*sc_if;
	struct mii_data		*mii;

	sc_if = ifp->if_softc;
	mii = device_get_softc(sc_if->sk_miibus);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;

	return;
}

static int
sk_ioctl(ifp, command, data)
	struct ifnet		*ifp;
	u_long			command;
	caddr_t			data;
{
	struct sk_if_softc	*sc_if = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	int			error, mask;
	struct mii_data		*mii;

	error = 0;
	switch(command) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > SK_JUMBO_MTU)
			error = EINVAL;
		else if (ifp->if_mtu != ifr->ifr_mtu) {
			if (sc_if->sk_jumbo_disable != 0 &&
			    ifr->ifr_mtu > SK_MAX_FRAMELEN)
				error = EINVAL;
			else {
				SK_IF_LOCK(sc_if);
				ifp->if_mtu = ifr->ifr_mtu;
				if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
					ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
					sk_init_locked(sc_if);
				}
				SK_IF_UNLOCK(sc_if);
			}
		}
		break;
	case SIOCSIFFLAGS:
		SK_IF_LOCK(sc_if);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				if ((ifp->if_flags ^ sc_if->sk_if_flags)
				    & (IFF_PROMISC | IFF_ALLMULTI))
					sk_rxfilter(sc_if);
			} else
				sk_init_locked(sc_if);
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				sk_stop(sc_if);
		}
		sc_if->sk_if_flags = ifp->if_flags;
		SK_IF_UNLOCK(sc_if);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		SK_IF_LOCK(sc_if);
		if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			sk_rxfilter(sc_if);
		SK_IF_UNLOCK(sc_if);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc_if->sk_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	case SIOCSIFCAP:
		SK_IF_LOCK(sc_if);
		if (sc_if->sk_softc->sk_type == SK_GENESIS) {
			SK_IF_UNLOCK(sc_if);
			break;
		}
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (IFCAP_TXCSUM & ifp->if_capabilities) != 0) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if ((ifp->if_capenable & IFCAP_TXCSUM) != 0)
				ifp->if_hwassist |= SK_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~SK_CSUM_FEATURES;
		}
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (IFCAP_RXCSUM & ifp->if_capabilities) != 0) 
			ifp->if_capenable ^= IFCAP_RXCSUM;
		SK_IF_UNLOCK(sc_if);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

/*
 * Probe for a SysKonnect GEnesis chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
skc_probe(dev)
	device_t		dev;
{
	const struct sk_type	*t = sk_devs;

	while(t->sk_name != NULL) {
		if ((pci_get_vendor(dev) == t->sk_vid) &&
		    (pci_get_device(dev) == t->sk_did)) {
			/*
			 * Only attach to rev. 2 of the Linksys EG1032 adapter.
			 * Rev. 3 is supported by re(4).
			 */
			if ((t->sk_vid == VENDORID_LINKSYS) &&
				(t->sk_did == DEVICEID_LINKSYS_EG1032) &&
				(pci_get_subdevice(dev) !=
				 SUBDEVICEID_LINKSYS_EG1032_REV2)) {
				t++;
				continue;
			}
			device_set_desc(dev, t->sk_name);
			return (BUS_PROBE_DEFAULT);
		}
		t++;
	}

	return(ENXIO);
}

/*
 * Force the GEnesis into reset, then bring it out of reset.
 */
static void
sk_reset(sc)
	struct sk_softc		*sc;
{

	CSR_WRITE_2(sc, SK_CSR, SK_CSR_SW_RESET);
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_MASTER_RESET);
	if (SK_YUKON_FAMILY(sc->sk_type))
		CSR_WRITE_2(sc, SK_LINK_CTRL, SK_LINK_RESET_SET);

	DELAY(1000);
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_SW_UNRESET);
	DELAY(2);
	CSR_WRITE_2(sc, SK_CSR, SK_CSR_MASTER_UNRESET);
	if (SK_YUKON_FAMILY(sc->sk_type))
		CSR_WRITE_2(sc, SK_LINK_CTRL, SK_LINK_RESET_CLEAR);

	if (sc->sk_type == SK_GENESIS) {
		/* Configure packet arbiter */
		sk_win_write_2(sc, SK_PKTARB_CTL, SK_PKTARBCTL_UNRESET);
		sk_win_write_2(sc, SK_RXPA1_TINIT, SK_PKTARB_TIMEOUT);
		sk_win_write_2(sc, SK_TXPA1_TINIT, SK_PKTARB_TIMEOUT);
		sk_win_write_2(sc, SK_RXPA2_TINIT, SK_PKTARB_TIMEOUT);
		sk_win_write_2(sc, SK_TXPA2_TINIT, SK_PKTARB_TIMEOUT);
	}

	/* Enable RAM interface */
	sk_win_write_4(sc, SK_RAMCTL, SK_RAMCTL_UNRESET);

	/*
         * Configure interrupt moderation. The moderation timer
	 * defers interrupts specified in the interrupt moderation
	 * timer mask based on the timeout specified in the interrupt
	 * moderation timer init register. Each bit in the timer
	 * register represents one tick, so to specify a timeout in
	 * microseconds, we have to multiply by the correct number of
	 * ticks-per-microsecond.
	 */
	switch (sc->sk_type) {
	case SK_GENESIS:
		sc->sk_int_ticks = SK_IMTIMER_TICKS_GENESIS;
		break;
	default:
		sc->sk_int_ticks = SK_IMTIMER_TICKS_YUKON;
		break;
	}
	if (bootverbose)
		device_printf(sc->sk_dev, "interrupt moderation is %d us\n",
		    sc->sk_int_mod);
	sk_win_write_4(sc, SK_IMTIMERINIT, SK_IM_USECS(sc->sk_int_mod,
	    sc->sk_int_ticks));
	sk_win_write_4(sc, SK_IMMR, SK_ISR_TX1_S_EOF|SK_ISR_TX2_S_EOF|
	    SK_ISR_RX1_EOF|SK_ISR_RX2_EOF);
	sk_win_write_1(sc, SK_IMTIMERCTL, SK_IMCTL_START);

	return;
}

static int
sk_probe(dev)
	device_t		dev;
{
	struct sk_softc		*sc;

	sc = device_get_softc(device_get_parent(dev));

	/*
	 * Not much to do here. We always know there will be
	 * at least one XMAC present, and if there are two,
	 * skc_attach() will create a second device instance
	 * for us.
	 */
	switch (sc->sk_type) {
	case SK_GENESIS:
		device_set_desc(dev, "XaQti Corp. XMAC II");
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		device_set_desc(dev, "Marvell Semiconductor, Inc. Yukon");
		break;
	}

	return (BUS_PROBE_DEFAULT);
}

/*
 * Each XMAC chip is attached as a separate logical IP interface.
 * Single port cards will have only one logical interface of course.
 */
static int
sk_attach(dev)
	device_t		dev;
{
	struct sk_softc		*sc;
	struct sk_if_softc	*sc_if;
	struct ifnet		*ifp;
	u_int32_t		r;
	int			error, i, phy, port;
	u_char			eaddr[6];
	u_char			inv_mac[] = {0, 0, 0, 0, 0, 0};

	if (dev == NULL)
		return(EINVAL);

	error = 0;
	sc_if = device_get_softc(dev);
	sc = device_get_softc(device_get_parent(dev));
	port = *(int *)device_get_ivars(dev);

	sc_if->sk_if_dev = dev;
	sc_if->sk_port = port;
	sc_if->sk_softc = sc;
	sc->sk_if[port] = sc_if;
	if (port == SK_PORT_A)
		sc_if->sk_tx_bmu = SK_BMU_TXS_CSR0;
	if (port == SK_PORT_B)
		sc_if->sk_tx_bmu = SK_BMU_TXS_CSR1;

	callout_init_mtx(&sc_if->sk_tick_ch, &sc_if->sk_softc->sk_mtx, 0);
	callout_init_mtx(&sc_if->sk_watchdog_ch, &sc_if->sk_softc->sk_mtx, 0);

	if (sk_dma_alloc(sc_if) != 0) {
		error = ENOMEM;
		goto fail;
	}
	sk_dma_jumbo_alloc(sc_if);

	ifp = sc_if->sk_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(sc_if->sk_if_dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc_if;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	/*
	 * SK_GENESIS has a bug in checksum offload - From linux.
	 */
	if (sc_if->sk_softc->sk_type != SK_GENESIS) {
		ifp->if_capabilities = IFCAP_TXCSUM | IFCAP_RXCSUM;
		ifp->if_hwassist = 0;
	} else {
		ifp->if_capabilities = 0;
		ifp->if_hwassist = 0;
	}
	ifp->if_capenable = ifp->if_capabilities;
	/*
	 * Some revision of Yukon controller generates corrupted
	 * frame when TX checksum offloading is enabled.  The
	 * frame has a valid checksum value so payload might be
	 * modified during TX checksum calculation. Disable TX
	 * checksum offloading but give users chance to enable it
	 * when they know their controller works without problems
	 * with TX checksum offloading.
	 */
	ifp->if_capenable &= ~IFCAP_TXCSUM;
	ifp->if_ioctl = sk_ioctl;
	ifp->if_start = sk_start;
	ifp->if_init = sk_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, SK_TX_RING_CNT - 1);
	ifp->if_snd.ifq_drv_maxlen = SK_TX_RING_CNT - 1;
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
	SK_IF_LOCK(sc_if);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		eaddr[i] =
		    sk_win_read_1(sc, SK_MAC0_0 + (port * 8) + i);

	/* Verify whether the station address is invalid or not. */
	if (bcmp(eaddr, inv_mac, sizeof(inv_mac)) == 0) {
		device_printf(sc_if->sk_if_dev,
		    "Generating random ethernet address\n");
		r = arc4random();
		/*
		 * Set OUI to convenient locally assigned address.  'b'
		 * is 0x62, which has the locally assigned bit set, and
		 * the broadcast/multicast bit clear.
		 */
		eaddr[0] = 'b';
		eaddr[1] = 's';
		eaddr[2] = 'd';
		eaddr[3] = (r >> 16) & 0xff;
		eaddr[4] = (r >>  8) & 0xff;
		eaddr[5] = (r >>  0) & 0xff;
	}
	/*
	 * Set up RAM buffer addresses. The NIC will have a certain
	 * amount of SRAM on it, somewhere between 512K and 2MB. We
	 * need to divide this up a) between the transmitter and
 	 * receiver and b) between the two XMACs, if this is a
	 * dual port NIC. Our algotithm is to divide up the memory
	 * evenly so that everyone gets a fair share.
	 *
	 * Just to be contrary, Yukon2 appears to have separate memory
	 * for each MAC.
	 */
	if (sk_win_read_1(sc, SK_CONFIG) & SK_CONFIG_SINGLEMAC) {
		u_int32_t		chunk, val;

		chunk = sc->sk_ramsize / 2;
		val = sc->sk_rboff / sizeof(u_int64_t);
		sc_if->sk_rx_ramstart = val;
		val += (chunk / sizeof(u_int64_t));
		sc_if->sk_rx_ramend = val - 1;
		sc_if->sk_tx_ramstart = val;
		val += (chunk / sizeof(u_int64_t));
		sc_if->sk_tx_ramend = val - 1;
	} else {
		u_int32_t		chunk, val;

		chunk = sc->sk_ramsize / 4;
		val = (sc->sk_rboff + (chunk * 2 * sc_if->sk_port)) /
		    sizeof(u_int64_t);
		sc_if->sk_rx_ramstart = val;
		val += (chunk / sizeof(u_int64_t));
		sc_if->sk_rx_ramend = val - 1;
		sc_if->sk_tx_ramstart = val;
		val += (chunk / sizeof(u_int64_t));
		sc_if->sk_tx_ramend = val - 1;
	}

	/* Read and save PHY type and set PHY address */
	sc_if->sk_phytype = sk_win_read_1(sc, SK_EPROM1) & 0xF;
	if (!SK_YUKON_FAMILY(sc->sk_type)) {
		switch(sc_if->sk_phytype) {
		case SK_PHYTYPE_XMAC:
			sc_if->sk_phyaddr = SK_PHYADDR_XMAC;
			break;
		case SK_PHYTYPE_BCOM:
			sc_if->sk_phyaddr = SK_PHYADDR_BCOM;
			break;
		default:
			device_printf(sc->sk_dev, "unsupported PHY type: %d\n",
			    sc_if->sk_phytype);
			error = ENODEV;
			SK_IF_UNLOCK(sc_if);
			goto fail;
		}
	} else {
		if (sc_if->sk_phytype < SK_PHYTYPE_MARV_COPPER &&
		    sc->sk_pmd != 'S') {
			/* not initialized, punt */
			sc_if->sk_phytype = SK_PHYTYPE_MARV_COPPER;
			sc->sk_coppertype = 1;
		}

		sc_if->sk_phyaddr = SK_PHYADDR_MARV;

		if (!(sc->sk_coppertype))
			sc_if->sk_phytype = SK_PHYTYPE_MARV_FIBER;
	}

	/*
	 * Call MI attach routine.  Can't hold locks when calling into ether_*.
	 */
	SK_IF_UNLOCK(sc_if);
	ether_ifattach(ifp, eaddr);
	SK_IF_LOCK(sc_if);

	/*
	 * The hardware should be ready for VLAN_MTU by default:
	 * XMAC II has 0x8100 in VLAN Tag Level 1 register initially;
	 * YU_SMR_MFL_VLAN is set by this driver in Yukon.
	 *
	 */
        ifp->if_capabilities |= IFCAP_VLAN_MTU;
        ifp->if_capenable |= IFCAP_VLAN_MTU;
	/*
	 * Tell the upper layer(s) we support long frames.
	 * Must appear after the call to ether_ifattach() because
	 * ether_ifattach() sets ifi_hdrlen to the default value.
	 */
        ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	/*
	 * Do miibus setup.
	 */
	phy = MII_PHY_ANY;
	switch (sc->sk_type) {
	case SK_GENESIS:
		sk_init_xmac(sc_if);
		if (sc_if->sk_phytype == SK_PHYTYPE_XMAC)
			phy = 0;
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		sk_init_yukon(sc_if);
		phy = 0;
		break;
	}

	SK_IF_UNLOCK(sc_if);
	error = mii_attach(dev, &sc_if->sk_miibus, ifp, sk_ifmedia_upd,
	    sk_ifmedia_sts, BMSR_DEFCAPMASK, phy, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev, "attaching PHYs failed\n");
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error) {
		/* Access should be ok even though lock has been dropped */
		sc->sk_if[port] = NULL;
		sk_detach(dev);
	}

	return(error);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
skc_attach(dev)
	device_t		dev;
{
	struct sk_softc		*sc;
	int			error = 0, *port;
	uint8_t			skrs;
	const char		*pname = NULL;
	char			*revstr;

	sc = device_get_softc(dev);
	sc->sk_dev = dev;

	mtx_init(&sc->sk_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	mtx_init(&sc->sk_mii_mtx, "sk_mii_mutex", NULL, MTX_DEF);
	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	/* Allocate resources */
#ifdef SK_USEIOSPACE
	sc->sk_res_spec = sk_res_spec_io;
#else
	sc->sk_res_spec = sk_res_spec_mem;
#endif
	error = bus_alloc_resources(dev, sc->sk_res_spec, sc->sk_res);
	if (error) {
		if (sc->sk_res_spec == sk_res_spec_mem)
			sc->sk_res_spec = sk_res_spec_io;
		else
			sc->sk_res_spec = sk_res_spec_mem;
		error = bus_alloc_resources(dev, sc->sk_res_spec, sc->sk_res);
		if (error) {
			device_printf(dev, "couldn't allocate %s resources\n",
			    sc->sk_res_spec == sk_res_spec_mem ? "memory" :
			    "I/O");
			goto fail;
		}
	}

	sc->sk_type = sk_win_read_1(sc, SK_CHIPVER);
	sc->sk_rev = (sk_win_read_1(sc, SK_CONFIG) >> 4) & 0xf;

	/* Bail out if chip is not recognized. */
	if (sc->sk_type != SK_GENESIS && !SK_YUKON_FAMILY(sc->sk_type)) {
		device_printf(dev, "unknown device: chipver=%02x, rev=%x\n",
		    sc->sk_type, sc->sk_rev);
		error = ENXIO;
		goto fail;
	}

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
		SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
		OID_AUTO, "int_mod", CTLTYPE_INT|CTLFLAG_RW,
		&sc->sk_int_mod, 0, sysctl_hw_sk_int_mod, "I",
		"SK interrupt moderation");

	/* Pull in device tunables. */
	sc->sk_int_mod = SK_IM_DEFAULT;
	error = resource_int_value(device_get_name(dev), device_get_unit(dev),
		"int_mod", &sc->sk_int_mod);
	if (error == 0) {
		if (sc->sk_int_mod < SK_IM_MIN ||
		    sc->sk_int_mod > SK_IM_MAX) {
			device_printf(dev, "int_mod value out of range; "
			    "using default: %d\n", SK_IM_DEFAULT);
			sc->sk_int_mod = SK_IM_DEFAULT;
		}
	}

	/* Reset the adapter. */
	sk_reset(sc);

	skrs = sk_win_read_1(sc, SK_EPROM0);
	if (sc->sk_type == SK_GENESIS) {
		/* Read and save RAM size and RAMbuffer offset */
		switch(skrs) {
		case SK_RAMSIZE_512K_64:
			sc->sk_ramsize = 0x80000;
			sc->sk_rboff = SK_RBOFF_0;
			break;
		case SK_RAMSIZE_1024K_64:
			sc->sk_ramsize = 0x100000;
			sc->sk_rboff = SK_RBOFF_80000;
			break;
		case SK_RAMSIZE_1024K_128:
			sc->sk_ramsize = 0x100000;
			sc->sk_rboff = SK_RBOFF_0;
			break;
		case SK_RAMSIZE_2048K_128:
			sc->sk_ramsize = 0x200000;
			sc->sk_rboff = SK_RBOFF_0;
			break;
		default:
			device_printf(dev, "unknown ram size: %d\n", skrs);
			error = ENXIO;
			goto fail;
		}
	} else { /* SK_YUKON_FAMILY */
		if (skrs == 0x00)
			sc->sk_ramsize = 0x20000;
		else
			sc->sk_ramsize = skrs * (1<<12);
		sc->sk_rboff = SK_RBOFF_0;
	}

	/* Read and save physical media type */
	 sc->sk_pmd = sk_win_read_1(sc, SK_PMDTYPE);

	 if (sc->sk_pmd == 'T' || sc->sk_pmd == '1')
		 sc->sk_coppertype = 1;
	 else
		 sc->sk_coppertype = 0;

	/* Determine whether to name it with VPD PN or just make it up.
	 * Marvell Yukon VPD PN seems to freqently be bogus. */
	switch (pci_get_device(dev)) {
	case DEVICEID_SK_V1:
	case DEVICEID_BELKIN_5005:
	case DEVICEID_3COM_3C940:
	case DEVICEID_LINKSYS_EG1032:
	case DEVICEID_DLINK_DGE530T_A1:
	case DEVICEID_DLINK_DGE530T_B1:
		/* Stay with VPD PN. */
		(void) pci_get_vpd_ident(dev, &pname);
		break;
	case DEVICEID_SK_V2:
		/* YUKON VPD PN might bear no resemblance to reality. */
		switch (sc->sk_type) {
		case SK_GENESIS:
			/* Stay with VPD PN. */
			(void) pci_get_vpd_ident(dev, &pname);
			break;
		case SK_YUKON:
			pname = "Marvell Yukon Gigabit Ethernet";
			break;
		case SK_YUKON_LITE:
			pname = "Marvell Yukon Lite Gigabit Ethernet";
			break;
		case SK_YUKON_LP:
			pname = "Marvell Yukon LP Gigabit Ethernet";
			break;
		default:
			pname = "Marvell Yukon (Unknown) Gigabit Ethernet";
			break;
		}

		/* Yukon Lite Rev. A0 needs special test. */
		if (sc->sk_type == SK_YUKON || sc->sk_type == SK_YUKON_LP) {
			u_int32_t far;
			u_int8_t testbyte;

			/* Save flash address register before testing. */
			far = sk_win_read_4(sc, SK_EP_ADDR);

			sk_win_write_1(sc, SK_EP_ADDR+0x03, 0xff);
			testbyte = sk_win_read_1(sc, SK_EP_ADDR+0x03);

			if (testbyte != 0x00) {
				/* Yukon Lite Rev. A0 detected. */
				sc->sk_type = SK_YUKON_LITE;
				sc->sk_rev = SK_YUKON_LITE_REV_A0;
				/* Restore flash address register. */
				sk_win_write_4(sc, SK_EP_ADDR, far);
			}
		}
		break;
	default:
		device_printf(dev, "unknown device: vendor=%04x, device=%04x, "
			"chipver=%02x, rev=%x\n",
			pci_get_vendor(dev), pci_get_device(dev),
			sc->sk_type, sc->sk_rev);
		error = ENXIO;
		goto fail;
	}

	if (sc->sk_type == SK_YUKON_LITE) {
		switch (sc->sk_rev) {
		case SK_YUKON_LITE_REV_A0:
			revstr = "A0";
			break;
		case SK_YUKON_LITE_REV_A1:
			revstr = "A1";
			break;
		case SK_YUKON_LITE_REV_A3:
			revstr = "A3";
			break;
		default:
			revstr = "";
			break;
		}
	} else {
		revstr = "";
	}

	/* Announce the product name and more VPD data if there. */
	if (pname != NULL)
		device_printf(dev, "%s rev. %s(0x%x)\n",
			pname, revstr, sc->sk_rev);

	if (bootverbose) {
		device_printf(dev, "chip ver  = 0x%02x\n", sc->sk_type);
		device_printf(dev, "chip rev  = 0x%02x\n", sc->sk_rev);
		device_printf(dev, "SK_EPROM0 = 0x%02x\n", skrs);
		device_printf(dev, "SRAM size = 0x%06x\n", sc->sk_ramsize);
	}

	sc->sk_devs[SK_PORT_A] = device_add_child(dev, "sk", -1);
	if (sc->sk_devs[SK_PORT_A] == NULL) {
		device_printf(dev, "failed to add child for PORT_A\n");
		error = ENXIO;
		goto fail;
	}
	port = malloc(sizeof(int), M_DEVBUF, M_NOWAIT);
	if (port == NULL) {
		device_printf(dev, "failed to allocate memory for "
		    "ivars of PORT_A\n");
		error = ENXIO;
		goto fail;
	}
	*port = SK_PORT_A;
	device_set_ivars(sc->sk_devs[SK_PORT_A], port);

	if (!(sk_win_read_1(sc, SK_CONFIG) & SK_CONFIG_SINGLEMAC)) {
		sc->sk_devs[SK_PORT_B] = device_add_child(dev, "sk", -1);
		if (sc->sk_devs[SK_PORT_B] == NULL) {
			device_printf(dev, "failed to add child for PORT_B\n");
			error = ENXIO;
			goto fail;
		}
		port = malloc(sizeof(int), M_DEVBUF, M_NOWAIT);
		if (port == NULL) {
			device_printf(dev, "failed to allocate memory for "
			    "ivars of PORT_B\n");
			error = ENXIO;
			goto fail;
		}
		*port = SK_PORT_B;
		device_set_ivars(sc->sk_devs[SK_PORT_B], port);
	}

	/* Turn on the 'driver is loaded' LED. */
	CSR_WRITE_2(sc, SK_LED, SK_LED_GREEN_ON);

	error = bus_generic_attach(dev);
	if (error) {
		device_printf(dev, "failed to attach port(s)\n");
		goto fail;
	}

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->sk_res[1], INTR_TYPE_NET|INTR_MPSAFE,
	    NULL, sk_intr, sc, &sc->sk_intrhand);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		goto fail;
	}

fail:
	if (error)
		skc_detach(dev);

	return(error);
}

/*
 * Shutdown hardware and free up resources. This can be called any
 * time after the mutex has been initialized. It is called in both
 * the error case in attach and the normal detach case so it needs
 * to be careful about only freeing resources that have actually been
 * allocated.
 */
static int
sk_detach(dev)
	device_t		dev;
{
	struct sk_if_softc	*sc_if;
	struct ifnet		*ifp;

	sc_if = device_get_softc(dev);
	KASSERT(mtx_initialized(&sc_if->sk_softc->sk_mtx),
	    ("sk mutex not initialized in sk_detach"));
	SK_IF_LOCK(sc_if);

	ifp = sc_if->sk_ifp;
	/* These should only be active if attach_xmac succeeded */
	if (device_is_attached(dev)) {
		sk_stop(sc_if);
		/* Can't hold locks while calling detach */
		SK_IF_UNLOCK(sc_if);
		callout_drain(&sc_if->sk_tick_ch);
		callout_drain(&sc_if->sk_watchdog_ch);
		ether_ifdetach(ifp);
		SK_IF_LOCK(sc_if);
	}
	/*
	 * We're generally called from skc_detach() which is using
	 * device_delete_child() to get to here. It's already trashed
	 * miibus for us, so don't do it here or we'll panic.
	 */
	/*
	if (sc_if->sk_miibus != NULL)
		device_delete_child(dev, sc_if->sk_miibus);
	*/
	bus_generic_detach(dev);
	sk_dma_jumbo_free(sc_if);
	sk_dma_free(sc_if);
	SK_IF_UNLOCK(sc_if);
	if (ifp)
		if_free(ifp);

	return(0);
}

static int
skc_detach(dev)
	device_t		dev;
{
	struct sk_softc		*sc;

	sc = device_get_softc(dev);
	KASSERT(mtx_initialized(&sc->sk_mtx), ("sk mutex not initialized"));

	if (device_is_alive(dev)) {
		if (sc->sk_devs[SK_PORT_A] != NULL) {
			free(device_get_ivars(sc->sk_devs[SK_PORT_A]), M_DEVBUF);
			device_delete_child(dev, sc->sk_devs[SK_PORT_A]);
		}
		if (sc->sk_devs[SK_PORT_B] != NULL) {
			free(device_get_ivars(sc->sk_devs[SK_PORT_B]), M_DEVBUF);
			device_delete_child(dev, sc->sk_devs[SK_PORT_B]);
		}
		bus_generic_detach(dev);
	}

	if (sc->sk_intrhand)
		bus_teardown_intr(dev, sc->sk_res[1], sc->sk_intrhand);
	bus_release_resources(dev, sc->sk_res_spec, sc->sk_res);

	mtx_destroy(&sc->sk_mii_mtx);
	mtx_destroy(&sc->sk_mtx);

	return(0);
}

static bus_dma_tag_t
skc_get_dma_tag(device_t bus, device_t child __unused)
{

	return (bus_get_dma_tag(bus));
}

struct sk_dmamap_arg {
	bus_addr_t	sk_busaddr;
};

static void
sk_dmamap_cb(arg, segs, nseg, error)
	void			*arg;
	bus_dma_segment_t	*segs;
	int			nseg;
	int			error;
{
	struct sk_dmamap_arg	*ctx;

	if (error != 0)
		return;

	ctx = arg;
	ctx->sk_busaddr = segs[0].ds_addr;
}

/*
 * Allocate jumbo buffer storage. The SysKonnect adapters support
 * "jumbograms" (9K frames), although SysKonnect doesn't currently
 * use them in their drivers. In order for us to use them, we need
 * large 9K receive buffers, however standard mbuf clusters are only
 * 2048 bytes in size. Consequently, we need to allocate and manage
 * our own jumbo buffer pool. Fortunately, this does not require an
 * excessive amount of additional code.
 */
static int
sk_dma_alloc(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_dmamap_arg	ctx;
	struct sk_txdesc	*txd;
	struct sk_rxdesc	*rxd;
	int			error, i;

	/* create parent tag */
	/*
	 * XXX
	 * This driver should use BUS_SPACE_MAXADDR for lowaddr argument
	 * in bus_dma_tag_create(9) as the NIC would support DAC mode.
	 * However bz@ reported that it does not work on amd64 with > 4GB
	 * RAM. Until we have more clues of the breakage, disable DAC mode
	 * by limiting DMA address to be in 32bit address space.
	 */
	error = bus_dma_tag_create(
		    bus_get_dma_tag(sc_if->sk_if_dev),/* parent */
		    1, 0,			/* algnmnt, boundary */
		    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
		    0,				/* nsegments */
		    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->sk_cdata.sk_parent_tag);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to create parent DMA tag\n");
		goto fail;
	}

	/* create tag for Tx ring */
	error = bus_dma_tag_create(sc_if->sk_cdata.sk_parent_tag,/* parent */
		    SK_RING_ALIGN, 0,		/* algnmnt, boundary */
		    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    SK_TX_RING_SZ,		/* maxsize */
		    1,				/* nsegments */
		    SK_TX_RING_SZ,		/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->sk_cdata.sk_tx_ring_tag);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to allocate Tx ring DMA tag\n");
		goto fail;
	}

	/* create tag for Rx ring */
	error = bus_dma_tag_create(sc_if->sk_cdata.sk_parent_tag,/* parent */
		    SK_RING_ALIGN, 0,		/* algnmnt, boundary */
		    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    SK_RX_RING_SZ,		/* maxsize */
		    1,				/* nsegments */
		    SK_RX_RING_SZ,		/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->sk_cdata.sk_rx_ring_tag);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to allocate Rx ring DMA tag\n");
		goto fail;
	}

	/* create tag for Tx buffers */
	error = bus_dma_tag_create(sc_if->sk_cdata.sk_parent_tag,/* parent */
		    1, 0,			/* algnmnt, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    MCLBYTES * SK_MAXTXSEGS,	/* maxsize */
		    SK_MAXTXSEGS,		/* nsegments */
		    MCLBYTES,			/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->sk_cdata.sk_tx_tag);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to allocate Tx DMA tag\n");
		goto fail;
	}

	/* create tag for Rx buffers */
	error = bus_dma_tag_create(sc_if->sk_cdata.sk_parent_tag,/* parent */
		    1, 0,			/* algnmnt, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    MCLBYTES,			/* maxsize */
		    1,				/* nsegments */
		    MCLBYTES,			/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->sk_cdata.sk_rx_tag);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to allocate Rx DMA tag\n");
		goto fail;
	}

	/* allocate DMA'able memory and load the DMA map for Tx ring */
	error = bus_dmamem_alloc(sc_if->sk_cdata.sk_tx_ring_tag,
	    (void **)&sc_if->sk_rdata.sk_tx_ring, BUS_DMA_NOWAIT |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc_if->sk_cdata.sk_tx_ring_map);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to allocate DMA'able memory for Tx ring\n");
		goto fail;
	}

	ctx.sk_busaddr = 0;
	error = bus_dmamap_load(sc_if->sk_cdata.sk_tx_ring_tag,
	    sc_if->sk_cdata.sk_tx_ring_map, sc_if->sk_rdata.sk_tx_ring,
	    SK_TX_RING_SZ, sk_dmamap_cb, &ctx, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to load DMA'able memory for Tx ring\n");
		goto fail;
	}
	sc_if->sk_rdata.sk_tx_ring_paddr = ctx.sk_busaddr;

	/* allocate DMA'able memory and load the DMA map for Rx ring */
	error = bus_dmamem_alloc(sc_if->sk_cdata.sk_rx_ring_tag,
	    (void **)&sc_if->sk_rdata.sk_rx_ring, BUS_DMA_NOWAIT |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc_if->sk_cdata.sk_rx_ring_map);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to allocate DMA'able memory for Rx ring\n");
		goto fail;
	}

	ctx.sk_busaddr = 0;
	error = bus_dmamap_load(sc_if->sk_cdata.sk_rx_ring_tag,
	    sc_if->sk_cdata.sk_rx_ring_map, sc_if->sk_rdata.sk_rx_ring,
	    SK_RX_RING_SZ, sk_dmamap_cb, &ctx, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to load DMA'able memory for Rx ring\n");
		goto fail;
	}
	sc_if->sk_rdata.sk_rx_ring_paddr = ctx.sk_busaddr;

	/* create DMA maps for Tx buffers */
	for (i = 0; i < SK_TX_RING_CNT; i++) {
		txd = &sc_if->sk_cdata.sk_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc_if->sk_cdata.sk_tx_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc_if->sk_if_dev,
			    "failed to create Tx dmamap\n");
			goto fail;
		}
	}

	/* create DMA maps for Rx buffers */
	if ((error = bus_dmamap_create(sc_if->sk_cdata.sk_rx_tag, 0,
	    &sc_if->sk_cdata.sk_rx_sparemap)) != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to create spare Rx dmamap\n");
		goto fail;
	}
	for (i = 0; i < SK_RX_RING_CNT; i++) {
		rxd = &sc_if->sk_cdata.sk_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc_if->sk_cdata.sk_rx_tag, 0,
		    &rxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc_if->sk_if_dev,
			    "failed to create Rx dmamap\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static int
sk_dma_jumbo_alloc(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_dmamap_arg	ctx;
	struct sk_rxdesc	*jrxd;
	int			error, i;

	if (jumbo_disable != 0) {
		device_printf(sc_if->sk_if_dev, "disabling jumbo frame support\n");
		sc_if->sk_jumbo_disable = 1;
		return (0);
	}
	/* create tag for jumbo Rx ring */
	error = bus_dma_tag_create(sc_if->sk_cdata.sk_parent_tag,/* parent */
		    SK_RING_ALIGN, 0,		/* algnmnt, boundary */
		    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    SK_JUMBO_RX_RING_SZ,	/* maxsize */
		    1,				/* nsegments */
		    SK_JUMBO_RX_RING_SZ,	/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->sk_cdata.sk_jumbo_rx_ring_tag);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to allocate jumbo Rx ring DMA tag\n");
		goto jumbo_fail;
	}

	/* create tag for jumbo Rx buffers */
	error = bus_dma_tag_create(sc_if->sk_cdata.sk_parent_tag,/* parent */
		    1, 0,			/* algnmnt, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    MJUM9BYTES,			/* maxsize */
		    1,				/* nsegments */
		    MJUM9BYTES,			/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->sk_cdata.sk_jumbo_rx_tag);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to allocate jumbo Rx DMA tag\n");
		goto jumbo_fail;
	}

	/* allocate DMA'able memory and load the DMA map for jumbo Rx ring */
	error = bus_dmamem_alloc(sc_if->sk_cdata.sk_jumbo_rx_ring_tag,
	    (void **)&sc_if->sk_rdata.sk_jumbo_rx_ring, BUS_DMA_NOWAIT |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO,
	    &sc_if->sk_cdata.sk_jumbo_rx_ring_map);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to allocate DMA'able memory for jumbo Rx ring\n");
		goto jumbo_fail;
	}

	ctx.sk_busaddr = 0;
	error = bus_dmamap_load(sc_if->sk_cdata.sk_jumbo_rx_ring_tag,
	    sc_if->sk_cdata.sk_jumbo_rx_ring_map,
	    sc_if->sk_rdata.sk_jumbo_rx_ring, SK_JUMBO_RX_RING_SZ, sk_dmamap_cb,
	    &ctx, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to load DMA'able memory for jumbo Rx ring\n");
		goto jumbo_fail;
	}
	sc_if->sk_rdata.sk_jumbo_rx_ring_paddr = ctx.sk_busaddr;

	/* create DMA maps for jumbo Rx buffers */
	if ((error = bus_dmamap_create(sc_if->sk_cdata.sk_jumbo_rx_tag, 0,
	    &sc_if->sk_cdata.sk_jumbo_rx_sparemap)) != 0) {
		device_printf(sc_if->sk_if_dev,
		    "failed to create spare jumbo Rx dmamap\n");
		goto jumbo_fail;
	}
	for (i = 0; i < SK_JUMBO_RX_RING_CNT; i++) {
		jrxd = &sc_if->sk_cdata.sk_jumbo_rxdesc[i];
		jrxd->rx_m = NULL;
		jrxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc_if->sk_cdata.sk_jumbo_rx_tag, 0,
		    &jrxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc_if->sk_if_dev,
			    "failed to create jumbo Rx dmamap\n");
			goto jumbo_fail;
		}
	}

	return (0);

jumbo_fail:
	sk_dma_jumbo_free(sc_if);
	device_printf(sc_if->sk_if_dev, "disabling jumbo frame support due to "
	    "resource shortage\n");
	sc_if->sk_jumbo_disable = 1;
	return (0);
}

static void
sk_dma_free(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_txdesc	*txd;
	struct sk_rxdesc	*rxd;
	int			i;

	/* Tx ring */
	if (sc_if->sk_cdata.sk_tx_ring_tag) {
		if (sc_if->sk_rdata.sk_tx_ring_paddr)
			bus_dmamap_unload(sc_if->sk_cdata.sk_tx_ring_tag,
			    sc_if->sk_cdata.sk_tx_ring_map);
		if (sc_if->sk_rdata.sk_tx_ring)
			bus_dmamem_free(sc_if->sk_cdata.sk_tx_ring_tag,
			    sc_if->sk_rdata.sk_tx_ring,
			    sc_if->sk_cdata.sk_tx_ring_map);
		sc_if->sk_rdata.sk_tx_ring = NULL;
		sc_if->sk_rdata.sk_tx_ring_paddr = 0;
		bus_dma_tag_destroy(sc_if->sk_cdata.sk_tx_ring_tag);
		sc_if->sk_cdata.sk_tx_ring_tag = NULL;
	}
	/* Rx ring */
	if (sc_if->sk_cdata.sk_rx_ring_tag) {
		if (sc_if->sk_rdata.sk_rx_ring_paddr)
			bus_dmamap_unload(sc_if->sk_cdata.sk_rx_ring_tag,
			    sc_if->sk_cdata.sk_rx_ring_map);
		if (sc_if->sk_rdata.sk_rx_ring)
			bus_dmamem_free(sc_if->sk_cdata.sk_rx_ring_tag,
			    sc_if->sk_rdata.sk_rx_ring,
			    sc_if->sk_cdata.sk_rx_ring_map);
		sc_if->sk_rdata.sk_rx_ring = NULL;
		sc_if->sk_rdata.sk_rx_ring_paddr = 0;
		bus_dma_tag_destroy(sc_if->sk_cdata.sk_rx_ring_tag);
		sc_if->sk_cdata.sk_rx_ring_tag = NULL;
	}
	/* Tx buffers */
	if (sc_if->sk_cdata.sk_tx_tag) {
		for (i = 0; i < SK_TX_RING_CNT; i++) {
			txd = &sc_if->sk_cdata.sk_txdesc[i];
			if (txd->tx_dmamap) {
				bus_dmamap_destroy(sc_if->sk_cdata.sk_tx_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = NULL;
			}
		}
		bus_dma_tag_destroy(sc_if->sk_cdata.sk_tx_tag);
		sc_if->sk_cdata.sk_tx_tag = NULL;
	}
	/* Rx buffers */
	if (sc_if->sk_cdata.sk_rx_tag) {
		for (i = 0; i < SK_RX_RING_CNT; i++) {
			rxd = &sc_if->sk_cdata.sk_rxdesc[i];
			if (rxd->rx_dmamap) {
				bus_dmamap_destroy(sc_if->sk_cdata.sk_rx_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = NULL;
			}
		}
		if (sc_if->sk_cdata.sk_rx_sparemap) {
			bus_dmamap_destroy(sc_if->sk_cdata.sk_rx_tag,
			    sc_if->sk_cdata.sk_rx_sparemap);
			sc_if->sk_cdata.sk_rx_sparemap = NULL;
		}
		bus_dma_tag_destroy(sc_if->sk_cdata.sk_rx_tag);
		sc_if->sk_cdata.sk_rx_tag = NULL;
	}

	if (sc_if->sk_cdata.sk_parent_tag) {
		bus_dma_tag_destroy(sc_if->sk_cdata.sk_parent_tag);
		sc_if->sk_cdata.sk_parent_tag = NULL;
	}
}

static void
sk_dma_jumbo_free(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_rxdesc	*jrxd;
	int			i;

	/* jumbo Rx ring */
	if (sc_if->sk_cdata.sk_jumbo_rx_ring_tag) {
		if (sc_if->sk_rdata.sk_jumbo_rx_ring_paddr)
			bus_dmamap_unload(sc_if->sk_cdata.sk_jumbo_rx_ring_tag,
			    sc_if->sk_cdata.sk_jumbo_rx_ring_map);
		if (sc_if->sk_rdata.sk_jumbo_rx_ring)
			bus_dmamem_free(sc_if->sk_cdata.sk_jumbo_rx_ring_tag,
			    sc_if->sk_rdata.sk_jumbo_rx_ring,
			    sc_if->sk_cdata.sk_jumbo_rx_ring_map);
		sc_if->sk_rdata.sk_jumbo_rx_ring = NULL;
		sc_if->sk_rdata.sk_jumbo_rx_ring_paddr = 0;
		bus_dma_tag_destroy(sc_if->sk_cdata.sk_jumbo_rx_ring_tag);
		sc_if->sk_cdata.sk_jumbo_rx_ring_tag = NULL;
	}

	/* jumbo Rx buffers */
	if (sc_if->sk_cdata.sk_jumbo_rx_tag) {
		for (i = 0; i < SK_JUMBO_RX_RING_CNT; i++) {
			jrxd = &sc_if->sk_cdata.sk_jumbo_rxdesc[i];
			if (jrxd->rx_dmamap) {
				bus_dmamap_destroy(
				    sc_if->sk_cdata.sk_jumbo_rx_tag,
				    jrxd->rx_dmamap);
				jrxd->rx_dmamap = NULL;
			}
		}
		if (sc_if->sk_cdata.sk_jumbo_rx_sparemap) {
			bus_dmamap_destroy(sc_if->sk_cdata.sk_jumbo_rx_tag,
			    sc_if->sk_cdata.sk_jumbo_rx_sparemap);
			sc_if->sk_cdata.sk_jumbo_rx_sparemap = NULL;
		}
		bus_dma_tag_destroy(sc_if->sk_cdata.sk_jumbo_rx_tag);
		sc_if->sk_cdata.sk_jumbo_rx_tag = NULL;
	}
}

static void
sk_txcksum(ifp, m, f)
	struct ifnet		*ifp;
	struct mbuf		*m;
	struct sk_tx_desc	*f;
{
	struct ip		*ip;
	u_int16_t		offset;
	u_int8_t 		*p;

	offset = sizeof(struct ip) + ETHER_HDR_LEN;
	for(; m && m->m_len == 0; m = m->m_next)
		;
	if (m == NULL || m->m_len < ETHER_HDR_LEN) {
		if_printf(ifp, "%s: m_len < ETHER_HDR_LEN\n", __func__);
		/* checksum may be corrupted */
		goto sendit;
	}
	if (m->m_len < ETHER_HDR_LEN + sizeof(u_int32_t)) {
		if (m->m_len != ETHER_HDR_LEN) {
			if_printf(ifp, "%s: m_len != ETHER_HDR_LEN\n",
			    __func__);
			/* checksum may be corrupted */
			goto sendit;
		}
		for(m = m->m_next; m && m->m_len == 0; m = m->m_next)
			;
		if (m == NULL) {
			offset = sizeof(struct ip) + ETHER_HDR_LEN;
			/* checksum may be corrupted */
			goto sendit;
		}
		ip = mtod(m, struct ip *);
	} else {
		p = mtod(m, u_int8_t *);
		p += ETHER_HDR_LEN;
		ip = (struct ip *)p;
	}
	offset = (ip->ip_hl << 2) + ETHER_HDR_LEN;

sendit:
	f->sk_csum_startval = 0;
	f->sk_csum_start = htole32(((offset + m->m_pkthdr.csum_data) & 0xffff) |
	    (offset << 16));
}

static int
sk_encap(sc_if, m_head)
        struct sk_if_softc	*sc_if;
        struct mbuf		**m_head;
{
	struct sk_txdesc	*txd;
	struct sk_tx_desc	*f = NULL;
	struct mbuf		*m;
	bus_dma_segment_t	txsegs[SK_MAXTXSEGS];
	u_int32_t		cflags, frag, si, sk_ctl;
	int			error, i, nseg;

	SK_IF_LOCK_ASSERT(sc_if);

	if ((txd = STAILQ_FIRST(&sc_if->sk_cdata.sk_txfreeq)) == NULL)
		return (ENOBUFS);

	error = bus_dmamap_load_mbuf_sg(sc_if->sk_cdata.sk_tx_tag,
	    txd->tx_dmamap, *m_head, txsegs, &nseg, 0);
	if (error == EFBIG) {
		m = m_defrag(*m_head, M_NOWAIT);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOMEM);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc_if->sk_cdata.sk_tx_tag,
		    txd->tx_dmamap, *m_head, txsegs, &nseg, 0);
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
	if (sc_if->sk_cdata.sk_tx_cnt + nseg >= SK_TX_RING_CNT) {
		bus_dmamap_unload(sc_if->sk_cdata.sk_tx_tag, txd->tx_dmamap);
		return (ENOBUFS);
	}

	m = *m_head;
	if ((m->m_pkthdr.csum_flags & sc_if->sk_ifp->if_hwassist) != 0)
		cflags = SK_OPCODE_CSUM;
	else
		cflags = SK_OPCODE_DEFAULT;
	si = frag = sc_if->sk_cdata.sk_tx_prod;
	for (i = 0; i < nseg; i++) {
		f = &sc_if->sk_rdata.sk_tx_ring[frag];
		f->sk_data_lo = htole32(SK_ADDR_LO(txsegs[i].ds_addr));
		f->sk_data_hi = htole32(SK_ADDR_HI(txsegs[i].ds_addr));
		sk_ctl = txsegs[i].ds_len | cflags;
		if (i == 0) {
			if (cflags == SK_OPCODE_CSUM)
				sk_txcksum(sc_if->sk_ifp, m, f);
			sk_ctl |= SK_TXCTL_FIRSTFRAG;
		} else
			sk_ctl |= SK_TXCTL_OWN;
		f->sk_ctl = htole32(sk_ctl);
		sc_if->sk_cdata.sk_tx_cnt++;
		SK_INC(frag, SK_TX_RING_CNT);
	}
	sc_if->sk_cdata.sk_tx_prod = frag;

	/* set EOF on the last desciptor */
	frag = (frag + SK_TX_RING_CNT - 1) % SK_TX_RING_CNT;
	f = &sc_if->sk_rdata.sk_tx_ring[frag];
	f->sk_ctl |= htole32(SK_TXCTL_LASTFRAG | SK_TXCTL_EOF_INTR);

	/* turn the first descriptor ownership to NIC */
	f = &sc_if->sk_rdata.sk_tx_ring[si];
	f->sk_ctl |= htole32(SK_TXCTL_OWN);

	STAILQ_REMOVE_HEAD(&sc_if->sk_cdata.sk_txfreeq, tx_q);
	STAILQ_INSERT_TAIL(&sc_if->sk_cdata.sk_txbusyq, txd, tx_q);
	txd->tx_m = m;

	/* sync descriptors */
	bus_dmamap_sync(sc_if->sk_cdata.sk_tx_tag, txd->tx_dmamap,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc_if->sk_cdata.sk_tx_ring_tag,
	    sc_if->sk_cdata.sk_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

static void
sk_start(ifp)
	struct ifnet		*ifp;
{
	struct sk_if_softc *sc_if;

	sc_if = ifp->if_softc;

	SK_IF_LOCK(sc_if);
	sk_start_locked(ifp);
	SK_IF_UNLOCK(sc_if);

	return;
}

static void
sk_start_locked(ifp)
	struct ifnet		*ifp;
{
        struct sk_softc		*sc;
        struct sk_if_softc	*sc_if;
        struct mbuf		*m_head;
	int			enq;

	sc_if = ifp->if_softc;
	sc = sc_if->sk_softc;

	SK_IF_LOCK_ASSERT(sc_if);

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    sc_if->sk_cdata.sk_tx_cnt < SK_TX_RING_CNT - 1; ) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (sk_encap(sc_if, &m_head)) {
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
		BPF_MTAP(ifp, m_head);
	}

	if (enq > 0) {
		/* Transmit */
		CSR_WRITE_4(sc, sc_if->sk_tx_bmu, SK_TXBMU_TX_START);

		/* Set a timeout in case the chip goes out to lunch. */
		sc_if->sk_watchdog_timer = 5;
	}
}


static void
sk_watchdog(arg)
	void			*arg;
{
	struct sk_if_softc	*sc_if;
	struct ifnet		*ifp;

	ifp = arg;
	sc_if = ifp->if_softc;

	SK_IF_LOCK_ASSERT(sc_if);

	if (sc_if->sk_watchdog_timer == 0 || --sc_if->sk_watchdog_timer)
		goto done;

	/*
	 * Reclaim first as there is a possibility of losing Tx completion
	 * interrupts.
	 */
	sk_txeof(sc_if);
	if (sc_if->sk_cdata.sk_tx_cnt != 0) {
		if_printf(sc_if->sk_ifp, "watchdog timeout\n");
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		sk_init_locked(sc_if);
	}

done:
	callout_reset(&sc_if->sk_watchdog_ch, hz, sk_watchdog, ifp);

	return;
}

static int
skc_shutdown(dev)
	device_t		dev;
{
	struct sk_softc		*sc;

	sc = device_get_softc(dev);
	SK_LOCK(sc);

	/* Turn off the 'driver is loaded' LED. */
	CSR_WRITE_2(sc, SK_LED, SK_LED_GREEN_OFF);

	/*
	 * Reset the GEnesis controller. Doing this should also
	 * assert the resets on the attached XMAC(s).
	 */
	sk_reset(sc);
	SK_UNLOCK(sc);

	return (0);
}

static int
skc_suspend(dev)
	device_t		dev;
{
	struct sk_softc		*sc;
	struct sk_if_softc	*sc_if0, *sc_if1;
	struct ifnet		*ifp0 = NULL, *ifp1 = NULL;

	sc = device_get_softc(dev);

	SK_LOCK(sc);

	sc_if0 = sc->sk_if[SK_PORT_A];
	sc_if1 = sc->sk_if[SK_PORT_B];
	if (sc_if0 != NULL)
		ifp0 = sc_if0->sk_ifp;
	if (sc_if1 != NULL)
		ifp1 = sc_if1->sk_ifp;
	if (ifp0 != NULL)
		sk_stop(sc_if0);
	if (ifp1 != NULL)
		sk_stop(sc_if1);
	sc->sk_suspended = 1;

	SK_UNLOCK(sc);

	return (0);
}

static int
skc_resume(dev)
	device_t		dev;
{
	struct sk_softc		*sc;
	struct sk_if_softc	*sc_if0, *sc_if1;
	struct ifnet		*ifp0 = NULL, *ifp1 = NULL;

	sc = device_get_softc(dev);

	SK_LOCK(sc);

	sc_if0 = sc->sk_if[SK_PORT_A];
	sc_if1 = sc->sk_if[SK_PORT_B];
	if (sc_if0 != NULL)
		ifp0 = sc_if0->sk_ifp;
	if (sc_if1 != NULL)
		ifp1 = sc_if1->sk_ifp;
	if (ifp0 != NULL && ifp0->if_flags & IFF_UP)
		sk_init_locked(sc_if0);
	if (ifp1 != NULL && ifp1->if_flags & IFF_UP)
		sk_init_locked(sc_if1);
	sc->sk_suspended = 0;

	SK_UNLOCK(sc);

	return (0);
}

/*
 * According to the data sheet from SK-NET GENESIS the hardware can compute
 * two Rx checksums at the same time(Each checksum start position is
 * programmed in Rx descriptors). However it seems that TCP/UDP checksum
 * does not work at least on my Yukon hardware. I tried every possible ways
 * to get correct checksum value but couldn't get correct one. So TCP/UDP
 * checksum offload was disabled at the moment and only IP checksum offload
 * was enabled.
 * As nomral IP header size is 20 bytes I can't expect it would give an
 * increase in throughput. However it seems it doesn't hurt performance in
 * my testing. If there is a more detailed information for checksum secret
 * of the hardware in question please contact yongari@FreeBSD.org to add
 * TCP/UDP checksum offload support.
 */
static __inline void
sk_rxcksum(ifp, m, csum)
	struct ifnet		*ifp;
	struct mbuf		*m;
	u_int32_t		csum;
{
	struct ether_header	*eh;
	struct ip		*ip;
	int32_t			hlen, len, pktlen;
	u_int16_t		csum1, csum2, ipcsum;

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

	csum1 = htons(csum & 0xffff);
	csum2 = htons((csum >> 16) & 0xffff);
	ipcsum = in_addword(csum1, ~csum2 & 0xffff);
	/* checksum fixup for IP options */
	len = hlen - sizeof(struct ip);
	if (len > 0) {
		/*
		 * If the second checksum value is correct we can compute IP
		 * checksum with simple math. Unfortunately the second checksum
		 * value is wrong so we can't verify the checksum from the
		 * value(It seems there is some magic here to get correct
		 * value). If the second checksum value is correct it also
		 * means we can get TCP/UDP checksum) here. However, it still
		 * needs pseudo header checksum calculation due to hardware
		 * limitations.
		 */
		return;
	}
	m->m_pkthdr.csum_flags = CSUM_IP_CHECKED;
	if (ipcsum == 0xffff)
		m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
}

static __inline int
sk_rxvalid(sc, stat, len)
	struct sk_softc		*sc;
	u_int32_t		stat, len;
{

	if (sc->sk_type == SK_GENESIS) {
		if ((stat & XM_RXSTAT_ERRFRAME) == XM_RXSTAT_ERRFRAME ||
		    XM_RXSTAT_BYTES(stat) != len)
			return (0);
	} else {
		if ((stat & (YU_RXSTAT_CRCERR | YU_RXSTAT_LONGERR |
		    YU_RXSTAT_MIIERR | YU_RXSTAT_BADFC | YU_RXSTAT_GOODFC |
		    YU_RXSTAT_JABBER)) != 0 ||
		    (stat & YU_RXSTAT_RXOK) != YU_RXSTAT_RXOK ||
		    YU_RXSTAT_BYTES(stat) != len)
			return (0);
	}

	return (1);
}

static void
sk_rxeof(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_softc		*sc;
	struct mbuf		*m;
	struct ifnet		*ifp;
	struct sk_rx_desc	*cur_rx;
	struct sk_rxdesc	*rxd;
	int			cons, prog;
	u_int32_t		csum, rxstat, sk_ctl;

	sc = sc_if->sk_softc;
	ifp = sc_if->sk_ifp;

	SK_IF_LOCK_ASSERT(sc_if);

	bus_dmamap_sync(sc_if->sk_cdata.sk_rx_ring_tag,
	    sc_if->sk_cdata.sk_rx_ring_map, BUS_DMASYNC_POSTREAD);

	prog = 0;
	for (cons = sc_if->sk_cdata.sk_rx_cons; prog < SK_RX_RING_CNT;
	    prog++, SK_INC(cons, SK_RX_RING_CNT)) {
		cur_rx = &sc_if->sk_rdata.sk_rx_ring[cons];
		sk_ctl = le32toh(cur_rx->sk_ctl);
		if ((sk_ctl & SK_RXCTL_OWN) != 0)
			break;
		rxd = &sc_if->sk_cdata.sk_rxdesc[cons];
		rxstat = le32toh(cur_rx->sk_xmac_rxstat);

		if ((sk_ctl & (SK_RXCTL_STATUS_VALID | SK_RXCTL_FIRSTFRAG |
		    SK_RXCTL_LASTFRAG)) != (SK_RXCTL_STATUS_VALID |
		    SK_RXCTL_FIRSTFRAG | SK_RXCTL_LASTFRAG) ||
		    SK_RXBYTES(sk_ctl) < SK_MIN_FRAMELEN ||
		    SK_RXBYTES(sk_ctl) > SK_MAX_FRAMELEN ||
		    sk_rxvalid(sc, rxstat, SK_RXBYTES(sk_ctl)) == 0) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			sk_discard_rxbuf(sc_if, cons);
			continue;
		}

		m = rxd->rx_m;
		csum = le32toh(cur_rx->sk_csum);
		if (sk_newbuf(sc_if, cons) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			/* reuse old buffer */
			sk_discard_rxbuf(sc_if, cons);
			continue;
		}
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len = SK_RXBYTES(sk_ctl);
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
			sk_rxcksum(ifp, m, csum);
		SK_IF_UNLOCK(sc_if);
		(*ifp->if_input)(ifp, m);
		SK_IF_LOCK(sc_if);
	}

	if (prog > 0) {
		sc_if->sk_cdata.sk_rx_cons = cons;
		bus_dmamap_sync(sc_if->sk_cdata.sk_rx_ring_tag,
		    sc_if->sk_cdata.sk_rx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
}

static void
sk_jumbo_rxeof(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_softc		*sc;
	struct mbuf		*m;
	struct ifnet		*ifp;
	struct sk_rx_desc	*cur_rx;
	struct sk_rxdesc	*jrxd;
	int			cons, prog;
	u_int32_t		csum, rxstat, sk_ctl;

	sc = sc_if->sk_softc;
	ifp = sc_if->sk_ifp;

	SK_IF_LOCK_ASSERT(sc_if);

	bus_dmamap_sync(sc_if->sk_cdata.sk_jumbo_rx_ring_tag,
	    sc_if->sk_cdata.sk_jumbo_rx_ring_map, BUS_DMASYNC_POSTREAD);

	prog = 0;
	for (cons = sc_if->sk_cdata.sk_jumbo_rx_cons;
	    prog < SK_JUMBO_RX_RING_CNT;
	    prog++, SK_INC(cons, SK_JUMBO_RX_RING_CNT)) {
		cur_rx = &sc_if->sk_rdata.sk_jumbo_rx_ring[cons];
		sk_ctl = le32toh(cur_rx->sk_ctl);
		if ((sk_ctl & SK_RXCTL_OWN) != 0)
			break;
		jrxd = &sc_if->sk_cdata.sk_jumbo_rxdesc[cons];
		rxstat = le32toh(cur_rx->sk_xmac_rxstat);

		if ((sk_ctl & (SK_RXCTL_STATUS_VALID | SK_RXCTL_FIRSTFRAG |
		    SK_RXCTL_LASTFRAG)) != (SK_RXCTL_STATUS_VALID |
		    SK_RXCTL_FIRSTFRAG | SK_RXCTL_LASTFRAG) ||
		    SK_RXBYTES(sk_ctl) < SK_MIN_FRAMELEN ||
		    SK_RXBYTES(sk_ctl) > SK_JUMBO_FRAMELEN ||
		    sk_rxvalid(sc, rxstat, SK_RXBYTES(sk_ctl)) == 0) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			sk_discard_jumbo_rxbuf(sc_if, cons);
			continue;
		}

		m = jrxd->rx_m;
		csum = le32toh(cur_rx->sk_csum);
		if (sk_jumbo_newbuf(sc_if, cons) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			/* reuse old buffer */
			sk_discard_jumbo_rxbuf(sc_if, cons);
			continue;
		}
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len = SK_RXBYTES(sk_ctl);
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
			sk_rxcksum(ifp, m, csum);
		SK_IF_UNLOCK(sc_if);
		(*ifp->if_input)(ifp, m);
		SK_IF_LOCK(sc_if);
	}

	if (prog > 0) {
		sc_if->sk_cdata.sk_jumbo_rx_cons = cons;
		bus_dmamap_sync(sc_if->sk_cdata.sk_jumbo_rx_ring_tag,
		    sc_if->sk_cdata.sk_jumbo_rx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
}

static void
sk_txeof(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_txdesc	*txd;
	struct sk_tx_desc	*cur_tx;
	struct ifnet		*ifp;
	u_int32_t		idx, sk_ctl;

	ifp = sc_if->sk_ifp;

	txd = STAILQ_FIRST(&sc_if->sk_cdata.sk_txbusyq);
	if (txd == NULL)
		return;
	bus_dmamap_sync(sc_if->sk_cdata.sk_tx_ring_tag,
	    sc_if->sk_cdata.sk_tx_ring_map, BUS_DMASYNC_POSTREAD);
	/*
	 * Go through our tx ring and free mbufs for those
	 * frames that have been sent.
	 */
	for (idx = sc_if->sk_cdata.sk_tx_cons;; SK_INC(idx, SK_TX_RING_CNT)) {
		if (sc_if->sk_cdata.sk_tx_cnt <= 0)
			break;
		cur_tx = &sc_if->sk_rdata.sk_tx_ring[idx];
		sk_ctl = le32toh(cur_tx->sk_ctl);
		if (sk_ctl & SK_TXCTL_OWN)
			break;
		sc_if->sk_cdata.sk_tx_cnt--;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		if ((sk_ctl & SK_TXCTL_LASTFRAG) == 0)
			continue;
		bus_dmamap_sync(sc_if->sk_cdata.sk_tx_tag, txd->tx_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc_if->sk_cdata.sk_tx_tag, txd->tx_dmamap);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		m_freem(txd->tx_m);
		txd->tx_m = NULL;
		STAILQ_REMOVE_HEAD(&sc_if->sk_cdata.sk_txbusyq, tx_q);
		STAILQ_INSERT_TAIL(&sc_if->sk_cdata.sk_txfreeq, txd, tx_q);
		txd = STAILQ_FIRST(&sc_if->sk_cdata.sk_txbusyq);
	}
	sc_if->sk_cdata.sk_tx_cons = idx;
	sc_if->sk_watchdog_timer = sc_if->sk_cdata.sk_tx_cnt > 0 ? 5 : 0;

	bus_dmamap_sync(sc_if->sk_cdata.sk_tx_ring_tag,
	    sc_if->sk_cdata.sk_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static void
sk_tick(xsc_if)
	void			*xsc_if;
{
	struct sk_if_softc	*sc_if;
	struct mii_data		*mii;
	struct ifnet		*ifp;
	int			i;

	sc_if = xsc_if;
	ifp = sc_if->sk_ifp;
	mii = device_get_softc(sc_if->sk_miibus);

	if (!(ifp->if_flags & IFF_UP))
		return;

	if (sc_if->sk_phytype == SK_PHYTYPE_BCOM) {
		sk_intr_bcom(sc_if);
		return;
	}

	/*
	 * According to SysKonnect, the correct way to verify that
	 * the link has come back up is to poll bit 0 of the GPIO
	 * register three times. This pin has the signal from the
	 * link_sync pin connected to it; if we read the same link
	 * state 3 times in a row, we know the link is up.
	 */
	for (i = 0; i < 3; i++) {
		if (SK_XM_READ_2(sc_if, XM_GPIO) & XM_GPIO_GP0_SET)
			break;
	}

	if (i != 3) {
		callout_reset(&sc_if->sk_tick_ch, hz, sk_tick, sc_if);
		return;
	}

	/* Turn the GP0 interrupt back on. */
	SK_XM_CLRBIT_2(sc_if, XM_IMR, XM_IMR_GP0_SET);
	SK_XM_READ_2(sc_if, XM_ISR);
	mii_tick(mii);
	callout_stop(&sc_if->sk_tick_ch);
}

static void
sk_yukon_tick(xsc_if)
	void			*xsc_if;
{
	struct sk_if_softc	*sc_if;
	struct mii_data		*mii;

	sc_if = xsc_if;
	mii = device_get_softc(sc_if->sk_miibus);

	mii_tick(mii);
	callout_reset(&sc_if->sk_tick_ch, hz, sk_yukon_tick, sc_if);
}

static void
sk_intr_bcom(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct mii_data		*mii;
	struct ifnet		*ifp;
	int			status;
	mii = device_get_softc(sc_if->sk_miibus);
	ifp = sc_if->sk_ifp;

	SK_XM_CLRBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_TX_ENB|XM_MMUCMD_RX_ENB);

	/*
	 * Read the PHY interrupt register to make sure
	 * we clear any pending interrupts.
	 */
	status = sk_xmac_miibus_readreg(sc_if, SK_PHYADDR_BCOM, BRGPHY_MII_ISR);

	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		sk_init_xmac(sc_if);
		return;
	}

	if (status & (BRGPHY_ISR_LNK_CHG|BRGPHY_ISR_AN_PR)) {
		int			lstat;
		lstat = sk_xmac_miibus_readreg(sc_if, SK_PHYADDR_BCOM,
		    BRGPHY_MII_AUXSTS);

		if (!(lstat & BRGPHY_AUXSTS_LINK) && sc_if->sk_link) {
			mii_mediachg(mii);
			/* Turn off the link LED. */
			SK_IF_WRITE_1(sc_if, 0,
			    SK_LINKLED1_CTL, SK_LINKLED_OFF);
			sc_if->sk_link = 0;
		} else if (status & BRGPHY_ISR_LNK_CHG) {
			sk_xmac_miibus_writereg(sc_if, SK_PHYADDR_BCOM,
	    		    BRGPHY_MII_IMR, 0xFF00);
			mii_tick(mii);
			sc_if->sk_link = 1;
			/* Turn on the link LED. */
			SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL,
			    SK_LINKLED_ON|SK_LINKLED_LINKSYNC_OFF|
			    SK_LINKLED_BLINK_OFF);
		} else {
			mii_tick(mii);
			callout_reset(&sc_if->sk_tick_ch, hz, sk_tick, sc_if);
		}
	}

	SK_XM_SETBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_TX_ENB|XM_MMUCMD_RX_ENB);

	return;
}

static void
sk_intr_xmac(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_softc		*sc;
	u_int16_t		status;

	sc = sc_if->sk_softc;
	status = SK_XM_READ_2(sc_if, XM_ISR);

	/*
	 * Link has gone down. Start MII tick timeout to
	 * watch for link resync.
	 */
	if (sc_if->sk_phytype == SK_PHYTYPE_XMAC) {
		if (status & XM_ISR_GP0_SET) {
			SK_XM_SETBIT_2(sc_if, XM_IMR, XM_IMR_GP0_SET);
			callout_reset(&sc_if->sk_tick_ch, hz, sk_tick, sc_if);
		}

		if (status & XM_ISR_AUTONEG_DONE) {
			callout_reset(&sc_if->sk_tick_ch, hz, sk_tick, sc_if);
		}
	}

	if (status & XM_IMR_TX_UNDERRUN)
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_FLUSH_TXFIFO);

	if (status & XM_IMR_RX_OVERRUN)
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_FLUSH_RXFIFO);

	status = SK_XM_READ_2(sc_if, XM_ISR);

	return;
}

static void
sk_intr_yukon(sc_if)
	struct sk_if_softc	*sc_if;
{
	u_int8_t status;

	status = SK_IF_READ_1(sc_if, 0, SK_GMAC_ISR);
	/* RX overrun */
	if ((status & SK_GMAC_INT_RX_OVER) != 0) {
		SK_IF_WRITE_1(sc_if, 0, SK_RXMF1_CTRL_TEST,
		    SK_RFCTL_RX_FIFO_OVER);
	}
	/* TX underrun */
	if ((status & SK_GMAC_INT_TX_UNDER) != 0) {
		SK_IF_WRITE_1(sc_if, 0, SK_RXMF1_CTRL_TEST,
		    SK_TFCTL_TX_FIFO_UNDER);
	}
}

static void
sk_intr(xsc)
	void			*xsc;
{
	struct sk_softc		*sc = xsc;
	struct sk_if_softc	*sc_if0, *sc_if1;
	struct ifnet		*ifp0 = NULL, *ifp1 = NULL;
	u_int32_t		status;

	SK_LOCK(sc);

	status = CSR_READ_4(sc, SK_ISSR);
	if (status == 0 || status == 0xffffffff || sc->sk_suspended)
		goto done_locked;

	sc_if0 = sc->sk_if[SK_PORT_A];
	sc_if1 = sc->sk_if[SK_PORT_B];

	if (sc_if0 != NULL)
		ifp0 = sc_if0->sk_ifp;
	if (sc_if1 != NULL)
		ifp1 = sc_if1->sk_ifp;

	for (; (status &= sc->sk_intrmask) != 0;) {
		/* Handle receive interrupts first. */
		if (status & SK_ISR_RX1_EOF) {
			if (ifp0->if_mtu > SK_MAX_FRAMELEN)
				sk_jumbo_rxeof(sc_if0);
			else
				sk_rxeof(sc_if0);
			CSR_WRITE_4(sc, SK_BMU_RX_CSR0,
			    SK_RXBMU_CLR_IRQ_EOF|SK_RXBMU_RX_START);
		}
		if (status & SK_ISR_RX2_EOF) {
			if (ifp1->if_mtu > SK_MAX_FRAMELEN)
				sk_jumbo_rxeof(sc_if1);
			else
				sk_rxeof(sc_if1);
			CSR_WRITE_4(sc, SK_BMU_RX_CSR1,
			    SK_RXBMU_CLR_IRQ_EOF|SK_RXBMU_RX_START);
		}

		/* Then transmit interrupts. */
		if (status & SK_ISR_TX1_S_EOF) {
			sk_txeof(sc_if0);
			CSR_WRITE_4(sc, SK_BMU_TXS_CSR0, SK_TXBMU_CLR_IRQ_EOF);
		}
		if (status & SK_ISR_TX2_S_EOF) {
			sk_txeof(sc_if1);
			CSR_WRITE_4(sc, SK_BMU_TXS_CSR1, SK_TXBMU_CLR_IRQ_EOF);
		}

		/* Then MAC interrupts. */
		if (status & SK_ISR_MAC1 &&
		    ifp0->if_drv_flags & IFF_DRV_RUNNING) {
			if (sc->sk_type == SK_GENESIS)
				sk_intr_xmac(sc_if0);
			else
				sk_intr_yukon(sc_if0);
		}

		if (status & SK_ISR_MAC2 &&
		    ifp1->if_drv_flags & IFF_DRV_RUNNING) {
			if (sc->sk_type == SK_GENESIS)
				sk_intr_xmac(sc_if1);
			else
				sk_intr_yukon(sc_if1);
		}

		if (status & SK_ISR_EXTERNAL_REG) {
			if (ifp0 != NULL &&
			    sc_if0->sk_phytype == SK_PHYTYPE_BCOM)
				sk_intr_bcom(sc_if0);
			if (ifp1 != NULL &&
			    sc_if1->sk_phytype == SK_PHYTYPE_BCOM)
				sk_intr_bcom(sc_if1);
		}
		status = CSR_READ_4(sc, SK_ISSR);
	}

	CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);

	if (ifp0 != NULL && !IFQ_DRV_IS_EMPTY(&ifp0->if_snd))
		sk_start_locked(ifp0);
	if (ifp1 != NULL && !IFQ_DRV_IS_EMPTY(&ifp1->if_snd))
		sk_start_locked(ifp1);

done_locked:
	SK_UNLOCK(sc);
}

static void
sk_init_xmac(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_softc		*sc;
	struct ifnet		*ifp;
	u_int16_t		eaddr[(ETHER_ADDR_LEN+1)/2];
	static const struct sk_bcom_hack bhack[] = {
	{ 0x18, 0x0c20 }, { 0x17, 0x0012 }, { 0x15, 0x1104 }, { 0x17, 0x0013 },
	{ 0x15, 0x0404 }, { 0x17, 0x8006 }, { 0x15, 0x0132 }, { 0x17, 0x8006 },
	{ 0x15, 0x0232 }, { 0x17, 0x800D }, { 0x15, 0x000F }, { 0x18, 0x0420 },
	{ 0, 0 } };

	SK_IF_LOCK_ASSERT(sc_if);

	sc = sc_if->sk_softc;
	ifp = sc_if->sk_ifp;

	/* Unreset the XMAC. */
	SK_IF_WRITE_2(sc_if, 0, SK_TXF1_MACCTL, SK_TXMACCTL_XMAC_UNRESET);
	DELAY(1000);

	/* Reset the XMAC's internal state. */
	SK_XM_SETBIT_2(sc_if, XM_GPIO, XM_GPIO_RESETMAC);

	/* Save the XMAC II revision */
	sc_if->sk_xmac_rev = XM_XMAC_REV(SK_XM_READ_4(sc_if, XM_DEVID));

	/*
	 * Perform additional initialization for external PHYs,
	 * namely for the 1000baseTX cards that use the XMAC's
	 * GMII mode.
	 */
	if (sc_if->sk_phytype == SK_PHYTYPE_BCOM) {
		int			i = 0;
		u_int32_t		val;

		/* Take PHY out of reset. */
		val = sk_win_read_4(sc, SK_GPIO);
		if (sc_if->sk_port == SK_PORT_A)
			val |= SK_GPIO_DIR0|SK_GPIO_DAT0;
		else
			val |= SK_GPIO_DIR2|SK_GPIO_DAT2;
		sk_win_write_4(sc, SK_GPIO, val);

		/* Enable GMII mode on the XMAC. */
		SK_XM_SETBIT_2(sc_if, XM_HWCFG, XM_HWCFG_GMIIMODE);

		sk_xmac_miibus_writereg(sc_if, SK_PHYADDR_BCOM,
		    BRGPHY_MII_BMCR, BRGPHY_BMCR_RESET);
		DELAY(10000);
		sk_xmac_miibus_writereg(sc_if, SK_PHYADDR_BCOM,
		    BRGPHY_MII_IMR, 0xFFF0);

		/*
		 * Early versions of the BCM5400 apparently have
		 * a bug that requires them to have their reserved
		 * registers initialized to some magic values. I don't
		 * know what the numbers do, I'm just the messenger.
		 */
		if (sk_xmac_miibus_readreg(sc_if, SK_PHYADDR_BCOM, 0x03)
		    == 0x6041) {
			while(bhack[i].reg) {
				sk_xmac_miibus_writereg(sc_if, SK_PHYADDR_BCOM,
				    bhack[i].reg, bhack[i].val);
				i++;
			}
		}
	}

	/* Set station address */
	bcopy(IF_LLADDR(sc_if->sk_ifp), eaddr, ETHER_ADDR_LEN);
	SK_XM_WRITE_2(sc_if, XM_PAR0, eaddr[0]);
	SK_XM_WRITE_2(sc_if, XM_PAR1, eaddr[1]);
	SK_XM_WRITE_2(sc_if, XM_PAR2, eaddr[2]);
	SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_USE_STATION);

	if (ifp->if_flags & IFF_BROADCAST) {
		SK_XM_CLRBIT_4(sc_if, XM_MODE, XM_MODE_RX_NOBROAD);
	} else {
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_NOBROAD);
	}

	/* We don't need the FCS appended to the packet. */
	SK_XM_SETBIT_2(sc_if, XM_RXCMD, XM_RXCMD_STRIPFCS);

	/* We want short frames padded to 60 bytes. */
	SK_XM_SETBIT_2(sc_if, XM_TXCMD, XM_TXCMD_AUTOPAD);

	/*
	 * Enable the reception of all error frames. This is is
	 * a necessary evil due to the design of the XMAC. The
	 * XMAC's receive FIFO is only 8K in size, however jumbo
	 * frames can be up to 9000 bytes in length. When bad
	 * frame filtering is enabled, the XMAC's RX FIFO operates
	 * in 'store and forward' mode. For this to work, the
	 * entire frame has to fit into the FIFO, but that means
	 * that jumbo frames larger than 8192 bytes will be
	 * truncated. Disabling all bad frame filtering causes
	 * the RX FIFO to operate in streaming mode, in which
	 * case the XMAC will start transferring frames out of the
	 * RX FIFO as soon as the FIFO threshold is reached.
	 */
	if (ifp->if_mtu > SK_MAX_FRAMELEN) {
		SK_XM_SETBIT_4(sc_if, XM_MODE, XM_MODE_RX_BADFRAMES|
		    XM_MODE_RX_GIANTS|XM_MODE_RX_RUNTS|XM_MODE_RX_CRCERRS|
		    XM_MODE_RX_INRANGELEN);
		SK_XM_SETBIT_2(sc_if, XM_RXCMD, XM_RXCMD_BIGPKTOK);
	} else
		SK_XM_CLRBIT_2(sc_if, XM_RXCMD, XM_RXCMD_BIGPKTOK);

	/*
	 * Bump up the transmit threshold. This helps hold off transmit
	 * underruns when we're blasting traffic from both ports at once.
	 */
	SK_XM_WRITE_2(sc_if, XM_TX_REQTHRESH, SK_XM_TX_FIFOTHRESH);

	/* Set Rx filter */
	sk_rxfilter_genesis(sc_if);

	/* Clear and enable interrupts */
	SK_XM_READ_2(sc_if, XM_ISR);
	if (sc_if->sk_phytype == SK_PHYTYPE_XMAC)
		SK_XM_WRITE_2(sc_if, XM_IMR, XM_INTRS);
	else
		SK_XM_WRITE_2(sc_if, XM_IMR, 0xFFFF);

	/* Configure MAC arbiter */
	switch(sc_if->sk_xmac_rev) {
	case XM_XMAC_REV_B2:
		sk_win_write_1(sc, SK_RCINIT_RX1, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RCINIT_TX1, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RCINIT_RX2, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RCINIT_TX2, SK_RCINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_RX1, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_TX1, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_RX2, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_MINIT_TX2, SK_MINIT_XMAC_B2);
		sk_win_write_1(sc, SK_RECOVERY_CTL, SK_RECOVERY_XMAC_B2);
		break;
	case XM_XMAC_REV_C1:
		sk_win_write_1(sc, SK_RCINIT_RX1, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RCINIT_TX1, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RCINIT_RX2, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RCINIT_TX2, SK_RCINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_RX1, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_TX1, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_RX2, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_MINIT_TX2, SK_MINIT_XMAC_C1);
		sk_win_write_1(sc, SK_RECOVERY_CTL, SK_RECOVERY_XMAC_B2);
		break;
	default:
		break;
	}
	sk_win_write_2(sc, SK_MACARB_CTL,
	    SK_MACARBCTL_UNRESET|SK_MACARBCTL_FASTOE_OFF);

	sc_if->sk_link = 1;

	return;
}

static void
sk_init_yukon(sc_if)
	struct sk_if_softc	*sc_if;
{
	u_int32_t		phy, v;
	u_int16_t		reg;
	struct sk_softc		*sc;
	struct ifnet		*ifp;
	u_int8_t		*eaddr;
	int			i;

	SK_IF_LOCK_ASSERT(sc_if);

	sc = sc_if->sk_softc;
	ifp = sc_if->sk_ifp;

	if (sc->sk_type == SK_YUKON_LITE &&
	    sc->sk_rev >= SK_YUKON_LITE_REV_A3) {
		/*
		 * Workaround code for COMA mode, set PHY reset.
		 * Otherwise it will not correctly take chip out of
		 * powerdown (coma)
		 */
		v = sk_win_read_4(sc, SK_GPIO);
		v |= SK_GPIO_DIR9 | SK_GPIO_DAT9;
		sk_win_write_4(sc, SK_GPIO, v);
	}

	/* GMAC and GPHY Reset */
	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, SK_GPHY_RESET_SET);
	SK_IF_WRITE_4(sc_if, 0, SK_GMAC_CTRL, SK_GMAC_RESET_SET);
	DELAY(1000);

	if (sc->sk_type == SK_YUKON_LITE &&
	    sc->sk_rev >= SK_YUKON_LITE_REV_A3) {
		/*
		 * Workaround code for COMA mode, clear PHY reset
		 */
		v = sk_win_read_4(sc, SK_GPIO);
		v |= SK_GPIO_DIR9;
		v &= ~SK_GPIO_DAT9;
		sk_win_write_4(sc, SK_GPIO, v);
	}

	phy = SK_GPHY_INT_POL_HI | SK_GPHY_DIS_FC | SK_GPHY_DIS_SLEEP |
		SK_GPHY_ENA_XC | SK_GPHY_ANEG_ALL | SK_GPHY_ENA_PAUSE;

	if (sc->sk_coppertype)
		phy |= SK_GPHY_COPPER;
	else
		phy |= SK_GPHY_FIBER;

	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, phy | SK_GPHY_RESET_SET);
	DELAY(1000);
	SK_IF_WRITE_4(sc_if, 0, SK_GPHY_CTRL, phy | SK_GPHY_RESET_CLEAR);
	SK_IF_WRITE_4(sc_if, 0, SK_GMAC_CTRL, SK_GMAC_LOOP_OFF |
		      SK_GMAC_PAUSE_ON | SK_GMAC_RESET_CLEAR);

	/* unused read of the interrupt source register */
	SK_IF_READ_2(sc_if, 0, SK_GMAC_ISR);

	reg = SK_YU_READ_2(sc_if, YUKON_PAR);

	/* MIB Counter Clear Mode set */
	reg |= YU_PAR_MIB_CLR;
	SK_YU_WRITE_2(sc_if, YUKON_PAR, reg);

	/* MIB Counter Clear Mode clear */
	reg &= ~YU_PAR_MIB_CLR;
	SK_YU_WRITE_2(sc_if, YUKON_PAR, reg);

	/* receive control reg */
	SK_YU_WRITE_2(sc_if, YUKON_RCR, YU_RCR_CRCR);

	/* transmit parameter register */
	SK_YU_WRITE_2(sc_if, YUKON_TPR, YU_TPR_JAM_LEN(0x3) |
		      YU_TPR_JAM_IPG(0xb) | YU_TPR_JAM2DATA_IPG(0x1a) );

	/* serial mode register */
	reg = YU_SMR_DATA_BLIND(0x1c) | YU_SMR_MFL_VLAN | YU_SMR_IPG_DATA(0x1e);
	if (ifp->if_mtu > SK_MAX_FRAMELEN)
		reg |= YU_SMR_MFL_JUMBO;
	SK_YU_WRITE_2(sc_if, YUKON_SMR, reg);

	/* Setup Yukon's station address */
	eaddr = IF_LLADDR(sc_if->sk_ifp);
	for (i = 0; i < 3; i++)
		SK_YU_WRITE_2(sc_if, SK_MAC0_0 + i * 4,
		    eaddr[i * 2] | eaddr[i * 2 + 1] << 8);
	/* Set GMAC source address of flow control. */
	for (i = 0; i < 3; i++)
		SK_YU_WRITE_2(sc_if, YUKON_SAL1 + i * 4,
		    eaddr[i * 2] | eaddr[i * 2 + 1] << 8);
	/* Set GMAC virtual address. */
	for (i = 0; i < 3; i++)
		SK_YU_WRITE_2(sc_if, YUKON_SAL2 + i * 4,
		    eaddr[i * 2] | eaddr[i * 2 + 1] << 8);

	/* Set Rx filter */
	sk_rxfilter_yukon(sc_if);

	/* enable interrupt mask for counter overflows */
	SK_YU_WRITE_2(sc_if, YUKON_TIMR, 0);
	SK_YU_WRITE_2(sc_if, YUKON_RIMR, 0);
	SK_YU_WRITE_2(sc_if, YUKON_TRIMR, 0);

	/* Configure RX MAC FIFO Flush Mask */
	v = YU_RXSTAT_FOFL | YU_RXSTAT_CRCERR | YU_RXSTAT_MIIERR |
	    YU_RXSTAT_BADFC | YU_RXSTAT_GOODFC | YU_RXSTAT_RUNT |
	    YU_RXSTAT_JABBER;
	SK_IF_WRITE_2(sc_if, 0, SK_RXMF1_FLUSH_MASK, v);

	/* Disable RX MAC FIFO Flush for YUKON-Lite Rev. A0 only */
	if (sc->sk_type == SK_YUKON_LITE && sc->sk_rev == SK_YUKON_LITE_REV_A0)
		v = SK_TFCTL_OPERATION_ON;
	else
		v = SK_TFCTL_OPERATION_ON | SK_RFCTL_FIFO_FLUSH_ON;
	/* Configure RX MAC FIFO */
	SK_IF_WRITE_1(sc_if, 0, SK_RXMF1_CTRL_TEST, SK_RFCTL_RESET_CLEAR);
	SK_IF_WRITE_2(sc_if, 0, SK_RXMF1_CTRL_TEST, v);

	/* Increase flush threshould to 64 bytes */
	SK_IF_WRITE_2(sc_if, 0, SK_RXMF1_FLUSH_THRESHOLD,
	    SK_RFCTL_FIFO_THRESHOLD + 1);

	/* Configure TX MAC FIFO */
	SK_IF_WRITE_1(sc_if, 0, SK_TXMF1_CTRL_TEST, SK_TFCTL_RESET_CLEAR);
	SK_IF_WRITE_2(sc_if, 0, SK_TXMF1_CTRL_TEST, SK_TFCTL_OPERATION_ON);
}

/*
 * Note that to properly initialize any part of the GEnesis chip,
 * you first have to take it out of reset mode.
 */
static void
sk_init(xsc)
	void			*xsc;
{
	struct sk_if_softc	*sc_if = xsc;

	SK_IF_LOCK(sc_if);
	sk_init_locked(sc_if);
	SK_IF_UNLOCK(sc_if);

	return;
}

static void
sk_init_locked(sc_if)
	struct sk_if_softc	*sc_if;
{
	struct sk_softc		*sc;
	struct ifnet		*ifp;
	struct mii_data		*mii;
	u_int16_t		reg;
	u_int32_t		imr;
	int			error;

	SK_IF_LOCK_ASSERT(sc_if);

	ifp = sc_if->sk_ifp;
	sc = sc_if->sk_softc;
	mii = device_get_softc(sc_if->sk_miibus);

	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return;

	/* Cancel pending I/O and free all RX/TX buffers. */
	sk_stop(sc_if);

	if (sc->sk_type == SK_GENESIS) {
		/* Configure LINK_SYNC LED */
		SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_ON);
		SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL,
			SK_LINKLED_LINKSYNC_ON);

		/* Configure RX LED */
		SK_IF_WRITE_1(sc_if, 0, SK_RXLED1_CTL,
			SK_RXLEDCTL_COUNTER_START);

		/* Configure TX LED */
		SK_IF_WRITE_1(sc_if, 0, SK_TXLED1_CTL,
			SK_TXLEDCTL_COUNTER_START);
	}

	/*
	 * Configure descriptor poll timer
	 *
	 * SK-NET GENESIS data sheet says that possibility of losing Start
	 * transmit command due to CPU/cache related interim storage problems
	 * under certain conditions. The document recommends a polling
	 * mechanism to send a Start transmit command to initiate transfer
	 * of ready descriptors regulary. To cope with this issue sk(4) now
	 * enables descriptor poll timer to initiate descriptor processing
	 * periodically as defined by SK_DPT_TIMER_MAX. However sk(4) still
	 * issue SK_TXBMU_TX_START to Tx BMU to get fast execution of Tx
	 * command instead of waiting for next descriptor polling time.
	 * The same rule may apply to Rx side too but it seems that is not
	 * needed at the moment.
	 * Since sk(4) uses descriptor polling as a last resort there is no
	 * need to set smaller polling time than maximum allowable one.
	 */
	SK_IF_WRITE_4(sc_if, 0, SK_DPT_INIT, SK_DPT_TIMER_MAX);

	/* Configure I2C registers */

	/* Configure XMAC(s) */
	switch (sc->sk_type) {
	case SK_GENESIS:
		sk_init_xmac(sc_if);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		sk_init_yukon(sc_if);
		break;
	}
	mii_mediachg(mii);

	if (sc->sk_type == SK_GENESIS) {
		/* Configure MAC FIFOs */
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_CTL, SK_FIFO_UNRESET);
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_END, SK_FIFO_END);
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_CTL, SK_FIFO_ON);

		SK_IF_WRITE_4(sc_if, 0, SK_TXF1_CTL, SK_FIFO_UNRESET);
		SK_IF_WRITE_4(sc_if, 0, SK_TXF1_END, SK_FIFO_END);
		SK_IF_WRITE_4(sc_if, 0, SK_TXF1_CTL, SK_FIFO_ON);
	}

	/* Configure transmit arbiter(s) */
	SK_IF_WRITE_1(sc_if, 0, SK_TXAR1_COUNTERCTL,
	    SK_TXARCTL_ON|SK_TXARCTL_FSYNC_ON);

	/* Configure RAMbuffers */
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_UNRESET);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_START, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_WR_PTR, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_RD_PTR, sc_if->sk_rx_ramstart);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_END, sc_if->sk_rx_ramend);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_ON);

	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_UNRESET);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_STORENFWD_ON);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_START, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_WR_PTR, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_RD_PTR, sc_if->sk_tx_ramstart);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_END, sc_if->sk_tx_ramend);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_ON);

	/* Configure BMUs */
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_ONLINE);
	if (ifp->if_mtu > SK_MAX_FRAMELEN) {
		SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_CURADDR_LO,
		    SK_ADDR_LO(SK_JUMBO_RX_RING_ADDR(sc_if, 0)));
		SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_CURADDR_HI,
		    SK_ADDR_HI(SK_JUMBO_RX_RING_ADDR(sc_if, 0)));
	} else {
		SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_CURADDR_LO,
		    SK_ADDR_LO(SK_RX_RING_ADDR(sc_if, 0)));
		SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_CURADDR_HI,
		    SK_ADDR_HI(SK_RX_RING_ADDR(sc_if, 0)));
	}

	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_BMU_CSR, SK_TXBMU_ONLINE);
	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_CURADDR_LO,
	    SK_ADDR_LO(SK_TX_RING_ADDR(sc_if, 0)));
	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_CURADDR_HI,
	    SK_ADDR_HI(SK_TX_RING_ADDR(sc_if, 0)));

	/* Init descriptors */
	if (ifp->if_mtu > SK_MAX_FRAMELEN)
		error = sk_init_jumbo_rx_ring(sc_if);
	else
		error = sk_init_rx_ring(sc_if);
	if (error != 0) {
		device_printf(sc_if->sk_if_dev,
		    "initialization failed: no memory for rx buffers\n");
		sk_stop(sc_if);
		return;
	}
	sk_init_tx_ring(sc_if);

	/* Set interrupt moderation if changed via sysctl. */
	imr = sk_win_read_4(sc, SK_IMTIMERINIT);
	if (imr != SK_IM_USECS(sc->sk_int_mod, sc->sk_int_ticks)) {
		sk_win_write_4(sc, SK_IMTIMERINIT, SK_IM_USECS(sc->sk_int_mod,
		    sc->sk_int_ticks));
		if (bootverbose)
			device_printf(sc_if->sk_if_dev,
			    "interrupt moderation is %d us.\n",
			    sc->sk_int_mod);
	}

	/* Configure interrupt handling */
	CSR_READ_4(sc, SK_ISSR);
	if (sc_if->sk_port == SK_PORT_A)
		sc->sk_intrmask |= SK_INTRS1;
	else
		sc->sk_intrmask |= SK_INTRS2;

	sc->sk_intrmask |= SK_ISR_EXTERNAL_REG;

	CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);

	/* Start BMUs. */
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_RX_START);

	switch(sc->sk_type) {
	case SK_GENESIS:
		/* Enable XMACs TX and RX state machines */
		SK_XM_CLRBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_IGNPAUSE);
		SK_XM_SETBIT_2(sc_if, XM_MMUCMD, XM_MMUCMD_TX_ENB|XM_MMUCMD_RX_ENB);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		reg = SK_YU_READ_2(sc_if, YUKON_GPCR);
		reg |= YU_GPCR_TXEN | YU_GPCR_RXEN;
#if 0
		/* XXX disable 100Mbps and full duplex mode? */
		reg &= ~(YU_GPCR_SPEED | YU_GPCR_DPLX_DIS);
#endif
		SK_YU_WRITE_2(sc_if, YUKON_GPCR, reg);
	}

	/* Activate descriptor polling timer */
	SK_IF_WRITE_4(sc_if, 0, SK_DPT_TIMER_CTRL, SK_DPT_TCTL_START);
	/* start transfer of Tx descriptors */
	CSR_WRITE_4(sc, sc_if->sk_tx_bmu, SK_TXBMU_TX_START);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	switch (sc->sk_type) {
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		callout_reset(&sc_if->sk_tick_ch, hz, sk_yukon_tick, sc_if);
		break;
	}

	callout_reset(&sc_if->sk_watchdog_ch, hz, sk_watchdog, ifp);

	return;
}

static void
sk_stop(sc_if)
	struct sk_if_softc	*sc_if;
{
	int			i;
	struct sk_softc		*sc;
	struct sk_txdesc	*txd;
	struct sk_rxdesc	*rxd;
	struct sk_rxdesc	*jrxd;
	struct ifnet		*ifp;
	u_int32_t		val;

	SK_IF_LOCK_ASSERT(sc_if);
	sc = sc_if->sk_softc;
	ifp = sc_if->sk_ifp;

	callout_stop(&sc_if->sk_tick_ch);
	callout_stop(&sc_if->sk_watchdog_ch);

	/* stop Tx descriptor polling timer */
	SK_IF_WRITE_4(sc_if, 0, SK_DPT_TIMER_CTRL, SK_DPT_TCTL_STOP);
	/* stop transfer of Tx descriptors */
	CSR_WRITE_4(sc, sc_if->sk_tx_bmu, SK_TXBMU_TX_STOP);
	for (i = 0; i < SK_TIMEOUT; i++) {
		val = CSR_READ_4(sc, sc_if->sk_tx_bmu);
		if ((val & SK_TXBMU_TX_STOP) == 0)
			break;
		DELAY(1);
	}
	if (i == SK_TIMEOUT)
		device_printf(sc_if->sk_if_dev,
		    "can not stop transfer of Tx descriptor\n");
	/* stop transfer of Rx descriptors */
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_RX_STOP);
	for (i = 0; i < SK_TIMEOUT; i++) {
		val = SK_IF_READ_4(sc_if, 0, SK_RXQ1_BMU_CSR);
		if ((val & SK_RXBMU_RX_STOP) == 0)
			break;
		DELAY(1);
	}
	if (i == SK_TIMEOUT)
		device_printf(sc_if->sk_if_dev,
		    "can not stop transfer of Rx descriptor\n");

	if (sc_if->sk_phytype == SK_PHYTYPE_BCOM) {
		/* Put PHY back into reset. */
		val = sk_win_read_4(sc, SK_GPIO);
		if (sc_if->sk_port == SK_PORT_A) {
			val |= SK_GPIO_DIR0;
			val &= ~SK_GPIO_DAT0;
		} else {
			val |= SK_GPIO_DIR2;
			val &= ~SK_GPIO_DAT2;
		}
		sk_win_write_4(sc, SK_GPIO, val);
	}

	/* Turn off various components of this interface. */
	SK_XM_SETBIT_2(sc_if, XM_GPIO, XM_GPIO_RESETMAC);
	switch (sc->sk_type) {
	case SK_GENESIS:
		SK_IF_WRITE_2(sc_if, 0, SK_TXF1_MACCTL, SK_TXMACCTL_XMAC_RESET);
		SK_IF_WRITE_4(sc_if, 0, SK_RXF1_CTL, SK_FIFO_RESET);
		break;
	case SK_YUKON:
	case SK_YUKON_LITE:
	case SK_YUKON_LP:
		SK_IF_WRITE_1(sc_if,0, SK_RXMF1_CTRL_TEST, SK_RFCTL_RESET_SET);
		SK_IF_WRITE_1(sc_if,0, SK_TXMF1_CTRL_TEST, SK_TFCTL_RESET_SET);
		break;
	}
	SK_IF_WRITE_4(sc_if, 0, SK_RXQ1_BMU_CSR, SK_RXBMU_OFFLINE);
	SK_IF_WRITE_4(sc_if, 0, SK_RXRB1_CTLTST, SK_RBCTL_RESET|SK_RBCTL_OFF);
	SK_IF_WRITE_4(sc_if, 1, SK_TXQS1_BMU_CSR, SK_TXBMU_OFFLINE);
	SK_IF_WRITE_4(sc_if, 1, SK_TXRBS1_CTLTST, SK_RBCTL_RESET|SK_RBCTL_OFF);
	SK_IF_WRITE_1(sc_if, 0, SK_TXAR1_COUNTERCTL, SK_TXARCTL_OFF);
	SK_IF_WRITE_1(sc_if, 0, SK_RXLED1_CTL, SK_RXLEDCTL_COUNTER_STOP);
	SK_IF_WRITE_1(sc_if, 0, SK_TXLED1_CTL, SK_RXLEDCTL_COUNTER_STOP);
	SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_OFF);
	SK_IF_WRITE_1(sc_if, 0, SK_LINKLED1_CTL, SK_LINKLED_LINKSYNC_OFF);

	/* Disable interrupts */
	if (sc_if->sk_port == SK_PORT_A)
		sc->sk_intrmask &= ~SK_INTRS1;
	else
		sc->sk_intrmask &= ~SK_INTRS2;
	CSR_WRITE_4(sc, SK_IMR, sc->sk_intrmask);

	SK_XM_READ_2(sc_if, XM_ISR);
	SK_XM_WRITE_2(sc_if, XM_IMR, 0xFFFF);

	/* Free RX and TX mbufs still in the queues. */
	for (i = 0; i < SK_RX_RING_CNT; i++) {
		rxd = &sc_if->sk_cdata.sk_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_sync(sc_if->sk_cdata.sk_rx_tag,
			    rxd->rx_dmamap, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc_if->sk_cdata.sk_rx_tag,
			    rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
	}
	for (i = 0; i < SK_JUMBO_RX_RING_CNT; i++) {
		jrxd = &sc_if->sk_cdata.sk_jumbo_rxdesc[i];
		if (jrxd->rx_m != NULL) {
			bus_dmamap_sync(sc_if->sk_cdata.sk_jumbo_rx_tag,
			    jrxd->rx_dmamap, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc_if->sk_cdata.sk_jumbo_rx_tag,
			    jrxd->rx_dmamap);
			m_freem(jrxd->rx_m);
			jrxd->rx_m = NULL;
		}
	}
	for (i = 0; i < SK_TX_RING_CNT; i++) {
		txd = &sc_if->sk_cdata.sk_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc_if->sk_cdata.sk_tx_tag,
			    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc_if->sk_cdata.sk_tx_tag,
			    txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
	}

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING|IFF_DRV_OACTIVE);

	return;
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
sysctl_hw_sk_int_mod(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_int_range(oidp, arg1, arg2, req, SK_IM_MIN, SK_IM_MAX));
}
