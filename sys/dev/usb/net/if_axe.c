/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999, 2000-2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 * ASIX Electronics AX88172/AX88178/AX88778 USB 2.0 ethernet driver.
 * Used in the LinkSys USB200M and various other adapters.
 *
 * Manuals available from:
 * http://www.asix.com.tw/datasheet/mac/Ax88172.PDF
 * Note: you need the manual for the AX88170 chip (USB 1.x ethernet
 * controller) to find the definitions for the RX control register.
 * http://www.asix.com.tw/datasheet/mac/Ax88170.PDF
 *
 * Written by Bill Paul <wpaul@windriver.com>
 * Senior Engineer
 * Wind River Systems
 */

/*
 * The AX88172 provides USB ethernet supports at 10 and 100Mbps.
 * It uses an external PHY (reference designs use a RealTek chip),
 * and has a 64-bit multicast hash filter. There is some information
 * missing from the manual which one needs to know in order to make
 * the chip function:
 *
 * - You must set bit 7 in the RX control register, otherwise the
 *   chip won't receive any packets.
 * - You must initialize all 3 IPG registers, or you won't be able
 *   to send any packets.
 *
 * Note that this device appears to only support loading the station
 * address via autload from the EEPROM (i.e. there's no way to manaully
 * set it).
 *
 * (Adam Weinberger wanted me to name this driver if_gir.c.)
 */

/*
 * Ax88178 and Ax88772 support backported from the OpenBSD driver.
 * 2007/02/12, J.R. Oldroyd, fbsd@opal.com
 *
 * Manual here:
 * http://www.asix.com.tw/FrootAttach/datasheet/AX88178_datasheet_Rev10.pdf
 * http://www.asix.com.tw/FrootAttach/datasheet/AX88772_datasheet_Rev10.pdf
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/sx.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/if_types.h>
#include <net/if_media.h>
#include <net/if_vlan_var.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR axe_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/net/usb_ethernet.h>
#include <dev/usb/net/if_axereg.h>

/*
 * AXE_178_MAX_FRAME_BURST
 * max frame burst size for Ax88178 and Ax88772
 *	0	2048 bytes
 *	1	4096 bytes
 *	2	8192 bytes
 *	3	16384 bytes
 * use the largest your system can handle without USB stalling.
 *
 * NB: 88772 parts appear to generate lots of input errors with
 * a 2K rx buffer and 8K is only slightly faster than 4K on an
 * EHCI port on a T42 so change at your own risk.
 */
#define AXE_178_MAX_FRAME_BURST	1

#define	AXE_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

#ifdef USB_DEBUG
static int axe_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, axe, CTLFLAG_RW, 0, "USB axe");
SYSCTL_INT(_hw_usb_axe, OID_AUTO, debug, CTLFLAG_RWTUN, &axe_debug, 0,
    "Debug level");
#endif

/*
 * Various supported device vendors/products.
 */
static const STRUCT_USB_HOST_ID axe_devs[] = {
#define	AXE_DEV(v,p,i) { USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, i) }
	AXE_DEV(ABOCOM, UF200, 0),
	AXE_DEV(ACERCM, EP1427X2, 0),
	AXE_DEV(APPLE, ETHERNET, AXE_FLAG_772),
	AXE_DEV(ASIX, AX88172, 0),
	AXE_DEV(ASIX, AX88178, AXE_FLAG_178),
	AXE_DEV(ASIX, AX88772, AXE_FLAG_772),
	AXE_DEV(ASIX, AX88772A, AXE_FLAG_772A),
	AXE_DEV(ASIX, AX88772B, AXE_FLAG_772B),
	AXE_DEV(ASIX, AX88772B_1, AXE_FLAG_772B),
	AXE_DEV(ATEN, UC210T, 0),
	AXE_DEV(BELKIN, F5D5055, AXE_FLAG_178),
	AXE_DEV(BILLIONTON, USB2AR, 0),
	AXE_DEV(CISCOLINKSYS, USB200MV2, AXE_FLAG_772A),
	AXE_DEV(COREGA, FETHER_USB2_TX, 0),
	AXE_DEV(DLINK, DUBE100, 0),
	AXE_DEV(DLINK, DUBE100B1, AXE_FLAG_772),
	AXE_DEV(DLINK, DUBE100C1, AXE_FLAG_772B),
	AXE_DEV(GOODWAY, GWUSB2E, 0),
	AXE_DEV(IODATA, ETGUS2, AXE_FLAG_178),
	AXE_DEV(JVC, MP_PRX1, 0),
	AXE_DEV(LENOVO, ETHERNET, AXE_FLAG_772B),
	AXE_DEV(LINKSYS2, USB200M, 0),
	AXE_DEV(LINKSYS4, USB1000, AXE_FLAG_178),
	AXE_DEV(LOGITEC, LAN_GTJU2A, AXE_FLAG_178),
	AXE_DEV(MELCO, LUAU2KTX, 0),
	AXE_DEV(MELCO, LUA3U2AGT, AXE_FLAG_178),
	AXE_DEV(NETGEAR, FA120, 0),
	AXE_DEV(OQO, ETHER01PLUS, AXE_FLAG_772),
	AXE_DEV(PLANEX3, GU1000T, AXE_FLAG_178),
	AXE_DEV(SITECOM, LN029, 0),
	AXE_DEV(SITECOMEU, LN028, AXE_FLAG_178),
	AXE_DEV(SITECOMEU, LN031, AXE_FLAG_178),
	AXE_DEV(SYSTEMTALKS, SGCX2UL, 0),
#undef AXE_DEV
};

static device_probe_t axe_probe;
static device_attach_t axe_attach;
static device_detach_t axe_detach;

static usb_callback_t axe_bulk_read_callback;
static usb_callback_t axe_bulk_write_callback;

static miibus_readreg_t axe_miibus_readreg;
static miibus_writereg_t axe_miibus_writereg;
static miibus_statchg_t axe_miibus_statchg;

static uether_fn_t axe_attach_post;
static uether_fn_t axe_init;
static uether_fn_t axe_stop;
static uether_fn_t axe_start;
static uether_fn_t axe_tick;
static uether_fn_t axe_setmulti;
static uether_fn_t axe_setpromisc;

static int	axe_attach_post_sub(struct usb_ether *);
static int	axe_ifmedia_upd(struct ifnet *);
static void	axe_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int	axe_cmd(struct axe_softc *, int, int, int, void *);
static void	axe_ax88178_init(struct axe_softc *);
static void	axe_ax88772_init(struct axe_softc *);
static void	axe_ax88772_phywake(struct axe_softc *);
static void	axe_ax88772a_init(struct axe_softc *);
static void	axe_ax88772b_init(struct axe_softc *);
static int	axe_get_phyno(struct axe_softc *, int);
static int	axe_ioctl(struct ifnet *, u_long, caddr_t);
static int	axe_rx_frame(struct usb_ether *, struct usb_page_cache *, int);
static int	axe_rxeof(struct usb_ether *, struct usb_page_cache *,
		    unsigned int offset, unsigned int, struct axe_csum_hdr *);
static void	axe_csum_cfg(struct usb_ether *);

static const struct usb_config axe_config[AXE_N_TRANSFER] = {

	[AXE_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.frames = 16,
		.bufsize = 16 * MCLBYTES,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = axe_bulk_write_callback,
		.timeout = 10000,	/* 10 seconds */
	},

	[AXE_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = 16384,	/* bytes */
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = axe_bulk_read_callback,
		.timeout = 0,	/* no timeout */
	},
};

static const struct ax88772b_mfb ax88772b_mfb_table[] = {
	{ 0x8000, 0x8001, 2048 },
	{ 0x8100, 0x8147, 4096},
	{ 0x8200, 0x81EB, 6144},
	{ 0x8300, 0x83D7, 8192},
	{ 0x8400, 0x851E, 16384},
	{ 0x8500, 0x8666, 20480},
	{ 0x8600, 0x87AE, 24576},
	{ 0x8700, 0x8A3D, 32768}
};

static device_method_t axe_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, axe_probe),
	DEVMETHOD(device_attach, axe_attach),
	DEVMETHOD(device_detach, axe_detach),

	/* MII interface */
	DEVMETHOD(miibus_readreg, axe_miibus_readreg),
	DEVMETHOD(miibus_writereg, axe_miibus_writereg),
	DEVMETHOD(miibus_statchg, axe_miibus_statchg),

	DEVMETHOD_END
};

static driver_t axe_driver = {
	.name = "axe",
	.methods = axe_methods,
	.size = sizeof(struct axe_softc),
};

static devclass_t axe_devclass;

DRIVER_MODULE(axe, uhub, axe_driver, axe_devclass, NULL, 0);
DRIVER_MODULE(miibus, axe, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(axe, uether, 1, 1, 1);
MODULE_DEPEND(axe, usb, 1, 1, 1);
MODULE_DEPEND(axe, ether, 1, 1, 1);
MODULE_DEPEND(axe, miibus, 1, 1, 1);
MODULE_VERSION(axe, 1);
USB_PNP_HOST_INFO(axe_devs);

static const struct usb_ether_methods axe_ue_methods = {
	.ue_attach_post = axe_attach_post,
	.ue_attach_post_sub = axe_attach_post_sub,
	.ue_start = axe_start,
	.ue_init = axe_init,
	.ue_stop = axe_stop,
	.ue_tick = axe_tick,
	.ue_setmulti = axe_setmulti,
	.ue_setpromisc = axe_setpromisc,
	.ue_mii_upd = axe_ifmedia_upd,
	.ue_mii_sts = axe_ifmedia_sts,
};

static int
axe_cmd(struct axe_softc *sc, int cmd, int index, int val, void *buf)
{
	struct usb_device_request req;
	usb_error_t err;

	AXE_LOCK_ASSERT(sc, MA_OWNED);

	req.bmRequestType = (AXE_CMD_IS_WRITE(cmd) ?
	    UT_WRITE_VENDOR_DEVICE :
	    UT_READ_VENDOR_DEVICE);
	req.bRequest = AXE_CMD_CMD(cmd);
	USETW(req.wValue, val);
	USETW(req.wIndex, index);
	USETW(req.wLength, AXE_CMD_LEN(cmd));

	err = uether_do_request(&sc->sc_ue, &req, buf, 1000);

	return (err);
}

static int
axe_miibus_readreg(device_t dev, int phy, int reg)
{
	struct axe_softc *sc = device_get_softc(dev);
	uint16_t val;
	int locked;

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		AXE_LOCK(sc);

	axe_cmd(sc, AXE_CMD_MII_OPMODE_SW, 0, 0, NULL);
	axe_cmd(sc, AXE_CMD_MII_READ_REG, reg, phy, &val);
	axe_cmd(sc, AXE_CMD_MII_OPMODE_HW, 0, 0, NULL);

	val = le16toh(val);
	if (AXE_IS_772(sc) && reg == MII_BMSR) {
		/*
		 * BMSR of AX88772 indicates that it supports extended
		 * capability but the extended status register is
		 * revered for embedded ethernet PHY. So clear the
		 * extended capability bit of BMSR.
		 */
		val &= ~BMSR_EXTCAP;
	}

	if (!locked)
		AXE_UNLOCK(sc);
	return (val);
}

static int
axe_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct axe_softc *sc = device_get_softc(dev);
	int locked;

	val = htole32(val);
	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		AXE_LOCK(sc);

	axe_cmd(sc, AXE_CMD_MII_OPMODE_SW, 0, 0, NULL);
	axe_cmd(sc, AXE_CMD_MII_WRITE_REG, reg, phy, &val);
	axe_cmd(sc, AXE_CMD_MII_OPMODE_HW, 0, 0, NULL);

	if (!locked)
		AXE_UNLOCK(sc);
	return (0);
}

static void
axe_miibus_statchg(device_t dev)
{
	struct axe_softc *sc = device_get_softc(dev);
	struct mii_data *mii = GET_MII(sc);
	struct ifnet *ifp;
	uint16_t val;
	int err, locked;

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		AXE_LOCK(sc);

	ifp = uether_getifp(&sc->sc_ue);
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		goto done;

	sc->sc_flags &= ~AXE_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->sc_flags |= AXE_FLAG_LINK;
			break;
		case IFM_1000_T:
			if ((sc->sc_flags & AXE_FLAG_178) == 0)
				break;
			sc->sc_flags |= AXE_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	/* Lost link, do nothing. */
	if ((sc->sc_flags & AXE_FLAG_LINK) == 0)
		goto done;

	val = 0;
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		val |= AXE_MEDIA_FULL_DUPLEX;
		if (AXE_IS_178_FAMILY(sc)) {
			if ((IFM_OPTIONS(mii->mii_media_active) &
			    IFM_ETH_TXPAUSE) != 0)
				val |= AXE_178_MEDIA_TXFLOW_CONTROL_EN;
			if ((IFM_OPTIONS(mii->mii_media_active) &
			    IFM_ETH_RXPAUSE) != 0)
				val |= AXE_178_MEDIA_RXFLOW_CONTROL_EN;
		}
	}
	if (AXE_IS_178_FAMILY(sc)) {
		val |= AXE_178_MEDIA_RX_EN | AXE_178_MEDIA_MAGIC;
		if ((sc->sc_flags & AXE_FLAG_178) != 0)
			val |= AXE_178_MEDIA_ENCK;
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_1000_T:
			val |= AXE_178_MEDIA_GMII | AXE_178_MEDIA_ENCK;
			break;
		case IFM_100_TX:
			val |= AXE_178_MEDIA_100TX;
			break;
		case IFM_10_T:
			/* doesn't need to be handled */
			break;
		}
	}
	err = axe_cmd(sc, AXE_CMD_WRITE_MEDIA, 0, val, NULL);
	if (err)
		device_printf(dev, "media change failed, error %d\n", err);
done:
	if (!locked)
		AXE_UNLOCK(sc);
}

/*
 * Set media options.
 */
static int
axe_ifmedia_upd(struct ifnet *ifp)
{
	struct axe_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);
	struct mii_softc *miisc;
	int error;

	AXE_LOCK_ASSERT(sc, MA_OWNED);

	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	return (error);
}

/*
 * Report current media status.
 */
static void
axe_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct axe_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);

	AXE_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	AXE_UNLOCK(sc);
}

static void
axe_setmulti(struct usb_ether *ue)
{
	struct axe_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);
	struct ifmultiaddr *ifma;
	uint32_t h = 0;
	uint16_t rxmode;
	uint8_t hashtbl[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	AXE_LOCK_ASSERT(sc, MA_OWNED);

	axe_cmd(sc, AXE_CMD_RXCTL_READ, 0, 0, &rxmode);
	rxmode = le16toh(rxmode);

	if (ifp->if_flags & (IFF_ALLMULTI | IFF_PROMISC)) {
		rxmode |= AXE_RXCMD_ALLMULTI;
		axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, rxmode, NULL);
		return;
	}
	rxmode &= ~AXE_RXCMD_ALLMULTI;

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link)
	{
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
		hashtbl[h / 8] |= 1 << (h % 8);
	}
	if_maddr_runlock(ifp);

	axe_cmd(sc, AXE_CMD_WRITE_MCAST, 0, 0, (void *)&hashtbl);
	axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, rxmode, NULL);
}

static int
axe_get_phyno(struct axe_softc *sc, int sel)
{
	int phyno;

	switch (AXE_PHY_TYPE(sc->sc_phyaddrs[sel])) {
	case PHY_TYPE_100_HOME:
	case PHY_TYPE_GIG:
		phyno = AXE_PHY_NO(sc->sc_phyaddrs[sel]);
		break;
	case PHY_TYPE_SPECIAL:
		/* FALLTHROUGH */
	case PHY_TYPE_RSVD:
		/* FALLTHROUGH */
	case PHY_TYPE_NON_SUP:
		/* FALLTHROUGH */
	default:
		phyno = -1;
		break;
	}

	return (phyno);
}

#define	AXE_GPIO_WRITE(x, y)	do {				\
	axe_cmd(sc, AXE_CMD_WRITE_GPIO, 0, (x), NULL);		\
	uether_pause(ue, (y));					\
} while (0)

static void
axe_ax88178_init(struct axe_softc *sc)
{
	struct usb_ether *ue;
	int gpio0, ledmode, phymode;
	uint16_t eeprom, val;

	ue = &sc->sc_ue;
	axe_cmd(sc, AXE_CMD_SROM_WR_ENABLE, 0, 0, NULL);
	/* XXX magic */
	axe_cmd(sc, AXE_CMD_SROM_READ, 0, 0x0017, &eeprom);
	eeprom = le16toh(eeprom);
	axe_cmd(sc, AXE_CMD_SROM_WR_DISABLE, 0, 0, NULL);

	/* if EEPROM is invalid we have to use to GPIO0 */
	if (eeprom == 0xffff) {
		phymode = AXE_PHY_MODE_MARVELL;
		gpio0 = 1;
		ledmode = 0;
	} else {
		phymode = eeprom & 0x7f;
		gpio0 = (eeprom & 0x80) ? 0 : 1;
		ledmode = eeprom >> 8;
	}

	if (bootverbose)
		device_printf(sc->sc_ue.ue_dev,
		    "EEPROM data : 0x%04x, phymode : 0x%02x\n", eeprom,
		    phymode);
	/* Program GPIOs depending on PHY hardware. */
	switch (phymode) {
	case AXE_PHY_MODE_MARVELL:
		if (gpio0 == 1) {
			AXE_GPIO_WRITE(AXE_GPIO_RELOAD_EEPROM | AXE_GPIO0_EN,
			    hz / 32);
			AXE_GPIO_WRITE(AXE_GPIO0_EN | AXE_GPIO2 | AXE_GPIO2_EN,
			    hz / 32);
			AXE_GPIO_WRITE(AXE_GPIO0_EN | AXE_GPIO2_EN, hz / 4);
			AXE_GPIO_WRITE(AXE_GPIO0_EN | AXE_GPIO2 | AXE_GPIO2_EN,
			    hz / 32);
		} else {
			AXE_GPIO_WRITE(AXE_GPIO_RELOAD_EEPROM | AXE_GPIO1 |
			    AXE_GPIO1_EN, hz / 3);
			if (ledmode == 1) {
				AXE_GPIO_WRITE(AXE_GPIO1_EN, hz / 3);
				AXE_GPIO_WRITE(AXE_GPIO1 | AXE_GPIO1_EN,
				    hz / 3);
			} else {
				AXE_GPIO_WRITE(AXE_GPIO1 | AXE_GPIO1_EN |
				    AXE_GPIO2 | AXE_GPIO2_EN, hz / 32);
				AXE_GPIO_WRITE(AXE_GPIO1 | AXE_GPIO1_EN |
				    AXE_GPIO2_EN, hz / 4);
				AXE_GPIO_WRITE(AXE_GPIO1 | AXE_GPIO1_EN |
				    AXE_GPIO2 | AXE_GPIO2_EN, hz / 32);
			}
		}
		break;
	case AXE_PHY_MODE_CICADA:
	case AXE_PHY_MODE_CICADA_V2:
	case AXE_PHY_MODE_CICADA_V2_ASIX:
		if (gpio0 == 1)
			AXE_GPIO_WRITE(AXE_GPIO_RELOAD_EEPROM | AXE_GPIO0 |
			    AXE_GPIO0_EN, hz / 32);
		else
			AXE_GPIO_WRITE(AXE_GPIO_RELOAD_EEPROM | AXE_GPIO1 |
			    AXE_GPIO1_EN, hz / 32);
		break;
	case AXE_PHY_MODE_AGERE:
		AXE_GPIO_WRITE(AXE_GPIO_RELOAD_EEPROM | AXE_GPIO1 |
		    AXE_GPIO1_EN, hz / 32);
		AXE_GPIO_WRITE(AXE_GPIO1 | AXE_GPIO1_EN | AXE_GPIO2 |
		    AXE_GPIO2_EN, hz / 32);
		AXE_GPIO_WRITE(AXE_GPIO1 | AXE_GPIO1_EN | AXE_GPIO2_EN, hz / 4);
		AXE_GPIO_WRITE(AXE_GPIO1 | AXE_GPIO1_EN | AXE_GPIO2 |
		    AXE_GPIO2_EN, hz / 32);
		break;
	case AXE_PHY_MODE_REALTEK_8211CL:
	case AXE_PHY_MODE_REALTEK_8211BN:
	case AXE_PHY_MODE_REALTEK_8251CL:
		val = gpio0 == 1 ? AXE_GPIO0 | AXE_GPIO0_EN :
		    AXE_GPIO1 | AXE_GPIO1_EN;
		AXE_GPIO_WRITE(val, hz / 32);
		AXE_GPIO_WRITE(val | AXE_GPIO2 | AXE_GPIO2_EN, hz / 32);
		AXE_GPIO_WRITE(val | AXE_GPIO2_EN, hz / 4);
		AXE_GPIO_WRITE(val | AXE_GPIO2 | AXE_GPIO2_EN, hz / 32);
		if (phymode == AXE_PHY_MODE_REALTEK_8211CL) {
			axe_miibus_writereg(ue->ue_dev, sc->sc_phyno,
			    0x1F, 0x0005);
			axe_miibus_writereg(ue->ue_dev, sc->sc_phyno,
			    0x0C, 0x0000);
			val = axe_miibus_readreg(ue->ue_dev, sc->sc_phyno,
			    0x0001);
			axe_miibus_writereg(ue->ue_dev, sc->sc_phyno,
			    0x01, val | 0x0080);
			axe_miibus_writereg(ue->ue_dev, sc->sc_phyno,
			    0x1F, 0x0000);
		}
		break;
	default:
		/* Unknown PHY model or no need to program GPIOs. */
		break;
	}

	/* soft reset */
	axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0, AXE_SW_RESET_CLEAR, NULL);
	uether_pause(ue, hz / 4);

	axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0,
	    AXE_SW_RESET_PRL | AXE_178_RESET_MAGIC, NULL);
	uether_pause(ue, hz / 4);
	/* Enable MII/GMII/RGMII interface to work with external PHY. */
	axe_cmd(sc, AXE_CMD_SW_PHY_SELECT, 0, 0, NULL);
	uether_pause(ue, hz / 4);

	axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, 0, NULL);
}

static void
axe_ax88772_init(struct axe_softc *sc)
{
	axe_cmd(sc, AXE_CMD_WRITE_GPIO, 0, 0x00b0, NULL);
	uether_pause(&sc->sc_ue, hz / 16);

	if (sc->sc_phyno == AXE_772_PHY_NO_EPHY) {
		/* ask for the embedded PHY */
		axe_cmd(sc, AXE_CMD_SW_PHY_SELECT, 0, 0x01, NULL);
		uether_pause(&sc->sc_ue, hz / 64);

		/* power down and reset state, pin reset state */
		axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0,
		    AXE_SW_RESET_CLEAR, NULL);
		uether_pause(&sc->sc_ue, hz / 16);

		/* power down/reset state, pin operating state */
		axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0,
		    AXE_SW_RESET_IPPD | AXE_SW_RESET_PRL, NULL);
		uether_pause(&sc->sc_ue, hz / 4);

		/* power up, reset */
		axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0, AXE_SW_RESET_PRL, NULL);

		/* power up, operating */
		axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0,
		    AXE_SW_RESET_IPRL | AXE_SW_RESET_PRL, NULL);
	} else {
		/* ask for external PHY */
		axe_cmd(sc, AXE_CMD_SW_PHY_SELECT, 0, 0x00, NULL);
		uether_pause(&sc->sc_ue, hz / 64);

		/* power down internal PHY */
		axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0,
		    AXE_SW_RESET_IPPD | AXE_SW_RESET_PRL, NULL);
	}

	uether_pause(&sc->sc_ue, hz / 4);
	axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, 0, NULL);
}

static void
axe_ax88772_phywake(struct axe_softc *sc)
{
	struct usb_ether *ue;

	ue = &sc->sc_ue;
	if (sc->sc_phyno == AXE_772_PHY_NO_EPHY) {
		/* Manually select internal(embedded) PHY - MAC mode. */
		axe_cmd(sc, AXE_CMD_SW_PHY_SELECT, 0, AXE_SW_PHY_SELECT_SS_ENB |
		    AXE_SW_PHY_SELECT_EMBEDDED | AXE_SW_PHY_SELECT_SS_MII,
		    NULL);
		uether_pause(&sc->sc_ue, hz / 32);
	} else {
		/*
		 * Manually select external PHY - MAC mode.
		 * Reverse MII/RMII is for AX88772A PHY mode.
		 */
		axe_cmd(sc, AXE_CMD_SW_PHY_SELECT, 0, AXE_SW_PHY_SELECT_SS_ENB |
		    AXE_SW_PHY_SELECT_EXT | AXE_SW_PHY_SELECT_SS_MII, NULL);
		uether_pause(&sc->sc_ue, hz / 32);
	}
	/* Take PHY out of power down. */
	axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0, AXE_SW_RESET_IPPD |
	    AXE_SW_RESET_IPRL, NULL);
	uether_pause(&sc->sc_ue, hz / 4);
	axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0, AXE_SW_RESET_IPRL, NULL);
	uether_pause(&sc->sc_ue, hz);
	axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0, AXE_SW_RESET_CLEAR, NULL);
	uether_pause(&sc->sc_ue, hz / 32);
	axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0, AXE_SW_RESET_IPRL, NULL);
	uether_pause(&sc->sc_ue, hz / 32);
}

static void
axe_ax88772a_init(struct axe_softc *sc)
{
	struct usb_ether *ue;

	ue = &sc->sc_ue;
	/* Reload EEPROM. */
	AXE_GPIO_WRITE(AXE_GPIO_RELOAD_EEPROM, hz / 32);
	axe_ax88772_phywake(sc);
	/* Stop MAC. */
	axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, 0, NULL);
}

static void
axe_ax88772b_init(struct axe_softc *sc)
{
	struct usb_ether *ue;
	uint16_t eeprom;
	uint8_t *eaddr;
	int i;

	ue = &sc->sc_ue;
	/* Reload EEPROM. */
	AXE_GPIO_WRITE(AXE_GPIO_RELOAD_EEPROM, hz / 32);
	/*
	 * Save PHY power saving configuration(high byte) and
	 * clear EEPROM checksum value(low byte).
	 */
	axe_cmd(sc, AXE_CMD_SROM_READ, 0, AXE_EEPROM_772B_PHY_PWRCFG, &eeprom);
	sc->sc_pwrcfg = le16toh(eeprom) & 0xFF00;

	/*
	 * Auto-loaded default station address from internal ROM is
	 * 00:00:00:00:00:00 such that an explicit access to EEPROM
	 * is required to get real station address.
	 */
	eaddr = ue->ue_eaddr;
	for (i = 0; i < ETHER_ADDR_LEN / 2; i++) {
		axe_cmd(sc, AXE_CMD_SROM_READ, 0, AXE_EEPROM_772B_NODE_ID + i,
		    &eeprom);
		eeprom = le16toh(eeprom);
		*eaddr++ = (uint8_t)(eeprom & 0xFF);
		*eaddr++ = (uint8_t)((eeprom >> 8) & 0xFF);
	}
	/* Wakeup PHY. */
	axe_ax88772_phywake(sc);
	/* Stop MAC. */
	axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, 0, NULL);
}

#undef	AXE_GPIO_WRITE

static void
axe_reset(struct axe_softc *sc)
{
	struct usb_config_descriptor *cd;
	usb_error_t err;

	cd = usbd_get_config_descriptor(sc->sc_ue.ue_udev);

	err = usbd_req_set_config(sc->sc_ue.ue_udev, &sc->sc_mtx,
	    cd->bConfigurationValue);
	if (err)
		DPRINTF("reset failed (ignored)\n");

	/* Wait a little while for the chip to get its brains in order. */
	uether_pause(&sc->sc_ue, hz / 100);

	/* Reinitialize controller to achieve full reset. */
	if (sc->sc_flags & AXE_FLAG_178)
		axe_ax88178_init(sc);
	else if (sc->sc_flags & AXE_FLAG_772)
		axe_ax88772_init(sc);
	else if (sc->sc_flags & AXE_FLAG_772A)
		axe_ax88772a_init(sc);
	else if (sc->sc_flags & AXE_FLAG_772B)
		axe_ax88772b_init(sc);
}

static void
axe_attach_post(struct usb_ether *ue)
{
	struct axe_softc *sc = uether_getsc(ue);

	/*
	 * Load PHY indexes first. Needed by axe_xxx_init().
	 */
	axe_cmd(sc, AXE_CMD_READ_PHYID, 0, 0, sc->sc_phyaddrs);
	if (bootverbose)
		device_printf(sc->sc_ue.ue_dev, "PHYADDR 0x%02x:0x%02x\n",
		    sc->sc_phyaddrs[0], sc->sc_phyaddrs[1]);
	sc->sc_phyno = axe_get_phyno(sc, AXE_PHY_SEL_PRI);
	if (sc->sc_phyno == -1)
		sc->sc_phyno = axe_get_phyno(sc, AXE_PHY_SEL_SEC);
	if (sc->sc_phyno == -1) {
		device_printf(sc->sc_ue.ue_dev,
		    "no valid PHY address found, assuming PHY address 0\n");
		sc->sc_phyno = 0;
	}

	/* Initialize controller and get station address. */
	if (sc->sc_flags & AXE_FLAG_178) {
		axe_ax88178_init(sc);
		axe_cmd(sc, AXE_178_CMD_READ_NODEID, 0, 0, ue->ue_eaddr);
	} else if (sc->sc_flags & AXE_FLAG_772) {
		axe_ax88772_init(sc);
		axe_cmd(sc, AXE_178_CMD_READ_NODEID, 0, 0, ue->ue_eaddr);
	} else if (sc->sc_flags & AXE_FLAG_772A) {
		axe_ax88772a_init(sc);
		axe_cmd(sc, AXE_178_CMD_READ_NODEID, 0, 0, ue->ue_eaddr);
	} else if (sc->sc_flags & AXE_FLAG_772B) {
		axe_ax88772b_init(sc);
	} else
		axe_cmd(sc, AXE_172_CMD_READ_NODEID, 0, 0, ue->ue_eaddr);

	/*
	 * Fetch IPG values.
	 */
	if (sc->sc_flags & (AXE_FLAG_772A | AXE_FLAG_772B)) {
		/* Set IPG values. */
		sc->sc_ipgs[0] = 0x15;
		sc->sc_ipgs[1] = 0x16;
		sc->sc_ipgs[2] = 0x1A;
	} else
		axe_cmd(sc, AXE_CMD_READ_IPG012, 0, 0, sc->sc_ipgs);
}

static int
axe_attach_post_sub(struct usb_ether *ue)
{
	struct axe_softc *sc;
	struct ifnet *ifp;
	u_int adv_pause;
	int error;

	sc = uether_getsc(ue);
	ifp = ue->ue_ifp;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = uether_start;
	ifp->if_ioctl = axe_ioctl;
	ifp->if_init = uether_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_drv_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	if (AXE_IS_178_FAMILY(sc))
		ifp->if_capabilities |= IFCAP_VLAN_MTU;
	if (sc->sc_flags & AXE_FLAG_772B) {
		ifp->if_capabilities |= IFCAP_TXCSUM | IFCAP_RXCSUM;
		ifp->if_hwassist = AXE_CSUM_FEATURES;
		/*
		 * Checksum offloading of AX88772B also works with VLAN
		 * tagged frames but there is no way to take advantage
		 * of the feature because vlan(4) assumes
		 * IFCAP_VLAN_HWTAGGING is prerequisite condition to
		 * support checksum offloading with VLAN. VLAN hardware
		 * tagging support of AX88772B is very limited so it's
		 * not possible to announce IFCAP_VLAN_HWTAGGING.
		 */
	}
	ifp->if_capenable = ifp->if_capabilities;
	if (sc->sc_flags & (AXE_FLAG_772A | AXE_FLAG_772B | AXE_FLAG_178))
		adv_pause = MIIF_DOPAUSE;
	else
		adv_pause = 0;
	mtx_lock(&Giant);
	error = mii_attach(ue->ue_dev, &ue->ue_miibus, ifp,
	    uether_ifmedia_upd, ue->ue_methods->ue_mii_sts,
	    BMSR_DEFCAPMASK, sc->sc_phyno, MII_OFFSET_ANY, adv_pause);
	mtx_unlock(&Giant);

	return (error);
}

/*
 * Probe for a AX88172 chip.
 */
static int
axe_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != AXE_CONFIG_IDX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != AXE_IFACE_IDX)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(axe_devs, sizeof(axe_devs), uaa));
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
axe_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct axe_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;
	uint8_t iface_index;
	int error;

	sc->sc_flags = USB_GET_DRIVER_INFO(uaa);

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	iface_index = AXE_IFACE_IDX;
	error = usbd_transfer_setup(uaa->device, &iface_index, sc->sc_xfer,
	    axe_config, AXE_N_TRANSFER, sc, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "allocating USB transfers failed\n");
		goto detach;
	}

	ue->ue_sc = sc;
	ue->ue_dev = dev;
	ue->ue_udev = uaa->device;
	ue->ue_mtx = &sc->sc_mtx;
	ue->ue_methods = &axe_ue_methods;

	error = uether_ifattach(ue);
	if (error) {
		device_printf(dev, "could not attach interface\n");
		goto detach;
	}
	return (0);			/* success */

detach:
	axe_detach(dev);
	return (ENXIO);			/* failure */
}

static int
axe_detach(device_t dev)
{
	struct axe_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;

	usbd_transfer_unsetup(sc->sc_xfer, AXE_N_TRANSFER);
	uether_ifdetach(ue);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

#if (AXE_BULK_BUF_SIZE >= 0x10000)
#error "Please update axe_bulk_read_callback()!"
#endif

static void
axe_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct axe_softc *sc = usbd_xfer_softc(xfer);
	struct usb_ether *ue = &sc->sc_ue;
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		axe_rx_frame(ue, pc, actlen);

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		uether_rxflush(ue);
		return;

	default:			/* Error */
		DPRINTF("bulk read error, %s\n", usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;

	}
}

static int
axe_rx_frame(struct usb_ether *ue, struct usb_page_cache *pc, int actlen)
{
	struct axe_softc *sc;
	struct axe_sframe_hdr hdr;
	struct axe_csum_hdr csum_hdr;
	int error, len, pos;

	sc = uether_getsc(ue);
	pos = 0;
	len = 0;
	error = 0;
	if ((sc->sc_flags & AXE_FLAG_STD_FRAME) != 0) {
		while (pos < actlen) {
			if ((int)(pos + sizeof(hdr)) > actlen) {
				/* too little data */
				error = EINVAL;
				break;
			}
			usbd_copy_out(pc, pos, &hdr, sizeof(hdr));

			if ((hdr.len ^ hdr.ilen) != sc->sc_lenmask) {
				/* we lost sync */
				error = EINVAL;
				break;
			}
			pos += sizeof(hdr);
			len = le16toh(hdr.len);
			if (pos + len > actlen) {
				/* invalid length */
				error = EINVAL;
				break;
			}
			axe_rxeof(ue, pc, pos, len, NULL);
			pos += len + (len % 2);
		}
	} else if ((sc->sc_flags & AXE_FLAG_CSUM_FRAME) != 0) {
		while (pos < actlen) {
			if ((int)(pos + sizeof(csum_hdr)) > actlen) {
				/* too little data */
				error = EINVAL;
				break;
			}
			usbd_copy_out(pc, pos, &csum_hdr, sizeof(csum_hdr));

			csum_hdr.len = le16toh(csum_hdr.len);
			csum_hdr.ilen = le16toh(csum_hdr.ilen);
			csum_hdr.cstatus = le16toh(csum_hdr.cstatus);
			if ((AXE_CSUM_RXBYTES(csum_hdr.len) ^
			    AXE_CSUM_RXBYTES(csum_hdr.ilen)) !=
			    sc->sc_lenmask) {
				/* we lost sync */
				error = EINVAL;
				break;
			}
			/*
			 * Get total transferred frame length including
			 * checksum header.  The length should be multiple
			 * of 4.
			 */
			len = sizeof(csum_hdr) + AXE_CSUM_RXBYTES(csum_hdr.len);
			len = (len + 3) & ~3;
			if (pos + len > actlen) {
				/* invalid length */
				error = EINVAL;
				break;
			}
			axe_rxeof(ue, pc, pos + sizeof(csum_hdr),
			    AXE_CSUM_RXBYTES(csum_hdr.len), &csum_hdr);
			pos += len;
		}
	} else
		axe_rxeof(ue, pc, 0, actlen, NULL);

	if (error != 0)
		if_inc_counter(ue->ue_ifp, IFCOUNTER_IERRORS, 1);
	return (error);
}

static int
axe_rxeof(struct usb_ether *ue, struct usb_page_cache *pc, unsigned int offset,
    unsigned int len, struct axe_csum_hdr *csum_hdr)
{
	struct ifnet *ifp = ue->ue_ifp;
	struct mbuf *m;

	if (len < ETHER_HDR_LEN || len > MCLBYTES - ETHER_ALIGN) {
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		return (EINVAL);
	}

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL) {
		if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
		return (ENOMEM);
	}
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, ETHER_ALIGN);

	usbd_copy_out(pc, offset, mtod(m, uint8_t *), len);

	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = len;

	if (csum_hdr != NULL && csum_hdr->cstatus & AXE_CSUM_HDR_L3_TYPE_IPV4) {
		if ((csum_hdr->cstatus & (AXE_CSUM_HDR_L4_CSUM_ERR |
		    AXE_CSUM_HDR_L3_CSUM_ERR)) == 0) {
			m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED |
			    CSUM_IP_VALID;
			if ((csum_hdr->cstatus & AXE_CSUM_HDR_L4_TYPE_MASK) ==
			    AXE_CSUM_HDR_L4_TYPE_TCP ||
			    (csum_hdr->cstatus & AXE_CSUM_HDR_L4_TYPE_MASK) ==
			    AXE_CSUM_HDR_L4_TYPE_UDP) {
				m->m_pkthdr.csum_flags |=
				    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
				m->m_pkthdr.csum_data = 0xffff;
			}
		}
	}

	_IF_ENQUEUE(&ue->ue_rxq, m);
	return (0);
}

#if ((AXE_BULK_BUF_SIZE >= 0x10000) || (AXE_BULK_BUF_SIZE < (MCLBYTES+4)))
#error "Please update axe_bulk_write_callback()!"
#endif

static void
axe_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct axe_softc *sc = usbd_xfer_softc(xfer);
	struct axe_sframe_hdr hdr;
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct usb_page_cache *pc;
	struct mbuf *m;
	int nframes, pos;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(11, "transfer complete\n");
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		if ((sc->sc_flags & AXE_FLAG_LINK) == 0 ||
		    (ifp->if_drv_flags & IFF_DRV_OACTIVE) != 0) {
			/*
			 * Don't send anything if there is no link or
			 * controller is busy.
			 */
			return;
		}

		for (nframes = 0; nframes < 16 &&
		    !IFQ_DRV_IS_EMPTY(&ifp->if_snd); nframes++) {
			IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
			if (m == NULL)
				break;
			usbd_xfer_set_frame_offset(xfer, nframes * MCLBYTES,
			    nframes);
			pos = 0;
			pc = usbd_xfer_get_frame(xfer, nframes);
			if (AXE_IS_178_FAMILY(sc)) {
				hdr.len = htole16(m->m_pkthdr.len);
				hdr.ilen = ~hdr.len;
				/*
				 * If upper stack computed checksum, driver
				 * should tell controller not to insert
				 * computed checksum for checksum offloading
				 * enabled controller.
				 */
				if (ifp->if_capabilities & IFCAP_TXCSUM) {
					if ((m->m_pkthdr.csum_flags &
					    AXE_CSUM_FEATURES) != 0)
						hdr.len |= htole16(
						    AXE_TX_CSUM_PSEUDO_HDR);
					else
						hdr.len |= htole16(
						    AXE_TX_CSUM_DIS);
				}
				usbd_copy_in(pc, pos, &hdr, sizeof(hdr));
				pos += sizeof(hdr);
				usbd_m_copy_in(pc, pos, m, 0, m->m_pkthdr.len);
				pos += m->m_pkthdr.len;
				if ((pos % 512) == 0) {
					hdr.len = 0;
					hdr.ilen = 0xffff;
					usbd_copy_in(pc, pos, &hdr,
					    sizeof(hdr));
					pos += sizeof(hdr);
				}
			} else {
				usbd_m_copy_in(pc, pos, m, 0, m->m_pkthdr.len);
				pos += m->m_pkthdr.len;
			}

			/*
			 * XXX
			 * Update TX packet counter here. This is not
			 * correct way but it seems that there is no way
			 * to know how many packets are sent at the end
			 * of transfer because controller combines
			 * multiple writes into single one if there is
			 * room in TX buffer of controller.
			 */
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

			/*
			 * if there's a BPF listener, bounce a copy
			 * of this frame to him:
			 */
			BPF_MTAP(ifp, m);

			m_freem(m);

			/* Set frame length. */
			usbd_xfer_set_frame_len(xfer, nframes, pos);
		}
		if (nframes != 0) {
			usbd_xfer_set_frames(xfer, nframes);
			usbd_transfer_submit(xfer);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
		}
		return;
		/* NOTREACHED */
	default:			/* Error */
		DPRINTFN(11, "transfer error, %s\n",
		    usbd_errstr(error));

		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;

	}
}

static void
axe_tick(struct usb_ether *ue)
{
	struct axe_softc *sc = uether_getsc(ue);
	struct mii_data *mii = GET_MII(sc);

	AXE_LOCK_ASSERT(sc, MA_OWNED);

	mii_tick(mii);
	if ((sc->sc_flags & AXE_FLAG_LINK) == 0) {
		axe_miibus_statchg(ue->ue_dev);
		if ((sc->sc_flags & AXE_FLAG_LINK) != 0)
			axe_start(ue);
	}
}

static void
axe_start(struct usb_ether *ue)
{
	struct axe_softc *sc = uether_getsc(ue);

	/*
	 * start the USB transfers, if not already started:
	 */
	usbd_transfer_start(sc->sc_xfer[AXE_BULK_DT_RD]);
	usbd_transfer_start(sc->sc_xfer[AXE_BULK_DT_WR]);
}

static void
axe_csum_cfg(struct usb_ether *ue)
{
	struct axe_softc *sc;
	struct ifnet *ifp;
	uint16_t csum1, csum2;

	sc = uether_getsc(ue);
	AXE_LOCK_ASSERT(sc, MA_OWNED);

	if ((sc->sc_flags & AXE_FLAG_772B) != 0) {
		ifp = uether_getifp(ue);
		csum1 = 0;
		csum2 = 0;
		if ((ifp->if_capenable & IFCAP_TXCSUM) != 0)
			csum1 |= AXE_TXCSUM_IP | AXE_TXCSUM_TCP |
			    AXE_TXCSUM_UDP;
		axe_cmd(sc, AXE_772B_CMD_WRITE_TXCSUM, csum2, csum1, NULL);
		csum1 = 0;
		csum2 = 0;
		if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
			csum1 |= AXE_RXCSUM_IP | AXE_RXCSUM_IPVE |
			    AXE_RXCSUM_TCP | AXE_RXCSUM_UDP | AXE_RXCSUM_ICMP |
			    AXE_RXCSUM_IGMP;
		axe_cmd(sc, AXE_772B_CMD_WRITE_RXCSUM, csum2, csum1, NULL);
	}
}

static void
axe_init(struct usb_ether *ue)
{
	struct axe_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);
	uint16_t rxmode;

	AXE_LOCK_ASSERT(sc, MA_OWNED);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	/* Cancel pending I/O */
	axe_stop(ue);

	axe_reset(sc);

	/* Set MAC address and transmitter IPG values. */
	if (AXE_IS_178_FAMILY(sc)) {
		axe_cmd(sc, AXE_178_CMD_WRITE_NODEID, 0, 0, IF_LLADDR(ifp));
		axe_cmd(sc, AXE_178_CMD_WRITE_IPG012, sc->sc_ipgs[2],
		    (sc->sc_ipgs[1] << 8) | (sc->sc_ipgs[0]), NULL);
	} else {
		axe_cmd(sc, AXE_172_CMD_WRITE_NODEID, 0, 0, IF_LLADDR(ifp));
		axe_cmd(sc, AXE_172_CMD_WRITE_IPG0, 0, sc->sc_ipgs[0], NULL);
		axe_cmd(sc, AXE_172_CMD_WRITE_IPG1, 0, sc->sc_ipgs[1], NULL);
		axe_cmd(sc, AXE_172_CMD_WRITE_IPG2, 0, sc->sc_ipgs[2], NULL);
	}

	if (AXE_IS_178_FAMILY(sc)) {
		sc->sc_flags &= ~(AXE_FLAG_STD_FRAME | AXE_FLAG_CSUM_FRAME);
		if ((sc->sc_flags & AXE_FLAG_772B) != 0 &&
		    (ifp->if_capenable & IFCAP_RXCSUM) != 0) {
			sc->sc_lenmask = AXE_CSUM_HDR_LEN_MASK;
			sc->sc_flags |= AXE_FLAG_CSUM_FRAME;
		} else {
			sc->sc_lenmask = AXE_HDR_LEN_MASK;
			sc->sc_flags |= AXE_FLAG_STD_FRAME;
		}
	}

	/* Configure TX/RX checksum offloading. */
	axe_csum_cfg(ue);

	if (sc->sc_flags & AXE_FLAG_772B) {
		/* AX88772B uses different maximum frame burst configuration. */
		axe_cmd(sc, AXE_772B_CMD_RXCTL_WRITE_CFG,
		    ax88772b_mfb_table[AX88772B_MFB_16K].threshold,
		    ax88772b_mfb_table[AX88772B_MFB_16K].byte_cnt, NULL);
	}

	/* Enable receiver, set RX mode. */
	rxmode = (AXE_RXCMD_MULTICAST | AXE_RXCMD_ENABLE);
	if (AXE_IS_178_FAMILY(sc)) {
		if (sc->sc_flags & AXE_FLAG_772B) {
			/*
			 * Select RX header format type 1.  Aligning IP
			 * header on 4 byte boundary is not needed when
			 * checksum offloading feature is not used
			 * because we always copy the received frame in
			 * RX handler.  When RX checksum offloading is
			 * active, aligning IP header is required to
			 * reflect actual frame length including RX
			 * header size.
			 */
			rxmode |= AXE_772B_RXCMD_HDR_TYPE_1;
			if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
				rxmode |= AXE_772B_RXCMD_IPHDR_ALIGN;
		} else {
			/*
			 * Default Rx buffer size is too small to get
			 * maximum performance.
			 */
			rxmode |= AXE_178_RXCMD_MFB_16384;
		}
	} else {
		rxmode |= AXE_172_RXCMD_UNICAST;
	}

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		rxmode |= AXE_RXCMD_PROMISC;

	if (ifp->if_flags & IFF_BROADCAST)
		rxmode |= AXE_RXCMD_BROADCAST;

	axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, rxmode, NULL);

	/* Load the multicast filter. */
	axe_setmulti(ue);

	usbd_xfer_set_stall(sc->sc_xfer[AXE_BULK_DT_WR]);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	/* Switch to selected media. */
	axe_ifmedia_upd(ifp);
}

static void
axe_setpromisc(struct usb_ether *ue)
{
	struct axe_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);
	uint16_t rxmode;

	axe_cmd(sc, AXE_CMD_RXCTL_READ, 0, 0, &rxmode);

	rxmode = le16toh(rxmode);

	if (ifp->if_flags & IFF_PROMISC) {
		rxmode |= AXE_RXCMD_PROMISC;
	} else {
		rxmode &= ~AXE_RXCMD_PROMISC;
	}

	axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, rxmode, NULL);

	axe_setmulti(ue);
}

static void
axe_stop(struct usb_ether *ue)
{
	struct axe_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	AXE_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->sc_flags &= ~AXE_FLAG_LINK;

	/*
	 * stop all the transfers, if not already stopped:
	 */
	usbd_transfer_stop(sc->sc_xfer[AXE_BULK_DT_WR]);
	usbd_transfer_stop(sc->sc_xfer[AXE_BULK_DT_RD]);
}

static int
axe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct usb_ether *ue = ifp->if_softc;
	struct axe_softc *sc;
	struct ifreq *ifr;
	int error, mask, reinit;

	sc = uether_getsc(ue);
	ifr = (struct ifreq *)data;
	error = 0;
	reinit = 0;
	if (cmd == SIOCSIFCAP) {
		AXE_LOCK(sc);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_TXCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if ((ifp->if_capenable & IFCAP_TXCSUM) != 0)
				ifp->if_hwassist |= AXE_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~AXE_CSUM_FEATURES;
			reinit++;
		}
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_RXCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_RXCSUM;
			reinit++;
		}
		if (reinit > 0 && ifp->if_drv_flags & IFF_DRV_RUNNING)
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		else
			reinit = 0;
		AXE_UNLOCK(sc);
		if (reinit > 0)
			uether_init(ue);
	} else
		error = uether_ioctl(ifp, cmd, data);

	return (error);
}
