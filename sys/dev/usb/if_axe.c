/*	$OpenBSD: if_axe.c,v 1.144 2024/09/04 07:54:52 mglocker Exp $	*/

/*
 * Copyright (c) 2005, 2006, 2007 Jonathan Gray <jsg@openbsd.org>
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

/*
 * ASIX Electronics AX88172 USB 2.0 ethernet driver. Used in the
 * LinkSys USB200M and various other adapters.
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
 * It uses an external PHY (reference designs use a Realtek chip),
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
 * address via autoload from the EEPROM (i.e. there's no way to manually
 * set it).
 *
 * (Adam Weinberger wanted me to name this driver if_gir.c.)
 */

/*
 * Ported to OpenBSD 3/28/2004 by Greg Taleck <taleck@oz.net>
 * with bits and pieces from the aue and url drivers.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/rwlock.h>
#include <sys/mbuf.h>

#include <sys/device.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/if_axereg.h>

#ifdef AXE_DEBUG
#define DPRINTF(x)	do { if (axedebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (axedebug >= (n)) printf x; } while (0)
int	axedebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

/*
 * Various supported device vendors/products.
 */
const struct axe_type axe_devs[] = {
	{ { USB_VENDOR_ABOCOM, USB_PRODUCT_ABOCOM_UF200}, 0 },
	{ { USB_VENDOR_ACERCM, USB_PRODUCT_ACERCM_EP1427X2}, 0 },
	{ { USB_VENDOR_APPLE, USB_PRODUCT_APPLE_ETHERNET }, AX772 },
	{ { USB_VENDOR_ASIX, USB_PRODUCT_ASIX_AX88172}, 0 },
	{ { USB_VENDOR_ASIX, USB_PRODUCT_ASIX_AX88772}, AX772 },
	{ { USB_VENDOR_ASIX, USB_PRODUCT_ASIX_AX88772A}, AX772 },
	{ { USB_VENDOR_ASIX, USB_PRODUCT_ASIX_AX88772B}, AX772 | AX772B },
	{ { USB_VENDOR_ASIX, USB_PRODUCT_ASIX_AX88772B_1}, AX772 | AX772B },
	{ { USB_VENDOR_ASIX, USB_PRODUCT_ASIX_AX88178}, AX178 },
	{ { USB_VENDOR_ATEN, USB_PRODUCT_ATEN_UC210T}, 0 },
	{ { USB_VENDOR_BELKIN, USB_PRODUCT_BELKIN_F5D5055 }, AX178 },
	{ { USB_VENDOR_BILLIONTON, USB_PRODUCT_BILLIONTON_USB2AR}, 0},
	{ { USB_VENDOR_CISCOLINKSYS, USB_PRODUCT_CISCOLINKSYS_USB200MV2}, AX772 },
	{ { USB_VENDOR_COREGA, USB_PRODUCT_COREGA_FETHER_USB2_TX }, 0},
	{ { USB_VENDOR_DLINK, USB_PRODUCT_DLINK_DUBE100}, 0 },
	{ { USB_VENDOR_DLINK, USB_PRODUCT_DLINK_DUBE100B1 }, AX772 },
	{ { USB_VENDOR_DLINK, USB_PRODUCT_DLINK_DUBE100C1 }, AX772 | AX772B },
	{ { USB_VENDOR_GOODWAY, USB_PRODUCT_GOODWAY_GWUSB2E}, 0 },
	{ { USB_VENDOR_IODATA, USB_PRODUCT_IODATA_ETGUS2 }, AX178 },
	{ { USB_VENDOR_JVC, USB_PRODUCT_JVC_MP_PRX1}, 0 },
	{ { USB_VENDOR_LENOVO, USB_PRODUCT_LENOVO_ETHERNET }, AX772 | AX772B },
	{ { USB_VENDOR_LINKSYS, USB_PRODUCT_LINKSYS_HG20F9}, AX772 | AX772B },
	{ { USB_VENDOR_LINKSYS2, USB_PRODUCT_LINKSYS2_USB200M}, 0 },
	{ { USB_VENDOR_LINKSYS4, USB_PRODUCT_LINKSYS4_USB1000 }, AX178 },
	{ { USB_VENDOR_LOGITEC, USB_PRODUCT_LOGITEC_LAN_GTJU2}, AX178 },
	{ { USB_VENDOR_MELCO, USB_PRODUCT_MELCO_LUAU2GT}, AX178 },
	{ { USB_VENDOR_MELCO, USB_PRODUCT_MELCO_LUAU2KTX}, 0 },
	{ { USB_VENDOR_MSI, USB_PRODUCT_MSI_AX88772A}, AX772 },
	{ { USB_VENDOR_NETGEAR, USB_PRODUCT_NETGEAR_FA120}, 0 },
	{ { USB_VENDOR_OQO, USB_PRODUCT_OQO_ETHER01PLUS }, AX772 },
	{ { USB_VENDOR_PLANEX3, USB_PRODUCT_PLANEX3_GU1000T }, AX178 },
	{ { USB_VENDOR_SYSTEMTALKS, USB_PRODUCT_SYSTEMTALKS_SGCX2UL}, 0 },
	{ { USB_VENDOR_SITECOM, USB_PRODUCT_SITECOM_LN029}, 0 },
	{ { USB_VENDOR_SITECOMEU, USB_PRODUCT_SITECOMEU_LN028 }, AX178 }
};

#define axe_lookup(v, p) ((struct axe_type *)usb_lookup(axe_devs, v, p))

int axe_match(struct device *, void *, void *);
void axe_attach(struct device *, struct device *, void *);
int axe_detach(struct device *, int);

struct cfdriver axe_cd = {
	NULL, "axe", DV_IFNET
};

const struct cfattach axe_ca = {
	sizeof(struct axe_softc), axe_match, axe_attach, axe_detach
};

int axe_tx_list_init(struct axe_softc *);
int axe_rx_list_init(struct axe_softc *);
struct mbuf *axe_newbuf(void);
int axe_encap(struct axe_softc *, struct mbuf *, int);
void axe_rxeof(struct usbd_xfer *, void *, usbd_status);
void axe_txeof(struct usbd_xfer *, void *, usbd_status);
void axe_tick(void *);
void axe_tick_task(void *);
void axe_start(struct ifnet *);
int axe_ioctl(struct ifnet *, u_long, caddr_t);
void axe_init(void *);
void axe_stop(struct axe_softc *);
void axe_watchdog(struct ifnet *);
int axe_miibus_readreg(struct device *, int, int);
void axe_miibus_writereg(struct device *, int, int, int);
void axe_miibus_statchg(struct device *);
int axe_cmd(struct axe_softc *, int, int, int, void *);
int axe_ifmedia_upd(struct ifnet *);
void axe_ifmedia_sts(struct ifnet *, struct ifmediareq *);
void axe_reset(struct axe_softc *sc);

void axe_iff(struct axe_softc *);
void axe_lock_mii(struct axe_softc *sc);
void axe_unlock_mii(struct axe_softc *sc);

void axe_ax88178_init(struct axe_softc *);
void axe_ax88772_init(struct axe_softc *);

/* Get exclusive access to the MII registers */
void
axe_lock_mii(struct axe_softc *sc)
{
	sc->axe_refcnt++;
	rw_enter_write(&sc->axe_mii_lock);
}

void
axe_unlock_mii(struct axe_softc *sc)
{
	rw_exit_write(&sc->axe_mii_lock);
	if (--sc->axe_refcnt < 0)
		usb_detach_wakeup(&sc->axe_dev);
}

int
axe_cmd(struct axe_softc *sc, int cmd, int index, int val, void *buf)
{
	usb_device_request_t	req;
	usbd_status		err;

	if (usbd_is_dying(sc->axe_udev))
		return(0);

	if (AXE_CMD_DIR(cmd))
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	else
		req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = AXE_CMD_CMD(cmd);
	USETW(req.wValue, val);
	USETW(req.wIndex, index);
	USETW(req.wLength, AXE_CMD_LEN(cmd));

	err = usbd_do_request(sc->axe_udev, &req, buf);

	if (err) {
		DPRINTF(("axe_cmd err: cmd: %d\n", cmd));
		return(-1);
	}

	return(0);
}

int
axe_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct axe_softc	*sc = (void *)dev;
	usbd_status		err;
	uWord			val;
	int			ival;

	if (usbd_is_dying(sc->axe_udev)) {
		DPRINTF(("axe: dying\n"));
		return(0);
	}

#ifdef notdef
	/*
	 * The chip tells us the MII address of any supported
	 * PHYs attached to the chip, so only read from those.
	 */

	DPRINTF(("axe_miibus_readreg: phy 0x%x reg 0x%x\n", phy, reg));

	if (sc->axe_phyaddrs[0] != AXE_NOPHY && phy != sc->axe_phyaddrs[0])
		return (0);

	if (sc->axe_phyaddrs[1] != AXE_NOPHY && phy != sc->axe_phyaddrs[1])
		return (0);
#endif
	if (sc->axe_phyno != phy)
		return (0);

	USETW(val, 0);

	axe_lock_mii(sc);
	axe_cmd(sc, AXE_CMD_MII_OPMODE_SW, 0, 0, NULL);
	err = axe_cmd(sc, AXE_CMD_MII_READ_REG, reg, phy, val);
	axe_cmd(sc, AXE_CMD_MII_OPMODE_HW, 0, 0, NULL);
	axe_unlock_mii(sc);

	if (err) {
		printf("axe%d: read PHY failed\n", sc->axe_unit);
		return(-1);
	}
	DPRINTF(("axe_miibus_readreg: phy 0x%x reg 0x%x val 0x%x\n",
	    phy, reg, UGETW(val)));

	ival = UGETW(val);
	if ((sc->axe_flags & AX772) != 0 && reg == MII_BMSR) {
		/*
		* BMSR of AX88772 indicates that it supports extended
		* capability but the extended status register is
		* reserved for embedded ethernet PHY. So clear the
		* extended capability bit of BMSR.
		*/
		ival &= ~BMSR_EXTCAP;
	}

	return (ival);
}

void
axe_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct axe_softc	*sc = (void *)dev;
	usbd_status		err;
	uWord			uval;

	if (usbd_is_dying(sc->axe_udev))
		return;
	if (sc->axe_phyno != phy)
		return;

	USETW(uval, val);

	axe_lock_mii(sc);
	axe_cmd(sc, AXE_CMD_MII_OPMODE_SW, 0, 0, NULL);
	err = axe_cmd(sc, AXE_CMD_MII_WRITE_REG, reg, phy, uval);
	axe_cmd(sc, AXE_CMD_MII_OPMODE_HW, 0, 0, NULL);
	axe_unlock_mii(sc);

	if (err) {
		printf("axe%d: write PHY failed\n", sc->axe_unit);
		return;
	}
}

void
axe_miibus_statchg(struct device *dev)
{
	struct axe_softc	*sc = (void *)dev;
	struct mii_data		*mii = GET_MII(sc);
	struct ifnet		*ifp;
	int			val, err;

	ifp = GET_IFP(sc);
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_flags & IFF_RUNNING) == 0)
		return;

	sc->axe_link = 0;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		    case IFM_10_T:
		    case IFM_100_TX:
			sc->axe_link++;
			break;
		    case IFM_1000_T:
			if ((sc->axe_flags & AX178) == 0)
			    break;
			sc->axe_link++;
			break;
		    default:
			break;
		}
	}

	/* Lost link, do nothing. */
	if (sc->axe_link == 0)
		return;

	val = 0;
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0)
		val |= AXE_MEDIA_FULL_DUPLEX;

	if (sc->axe_flags & AX178 || sc->axe_flags & AX772) {
		val |= (AXE_178_MEDIA_RX_EN | AXE_178_MEDIA_MAGIC);
		if (sc->axe_flags & AX178)
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

	DPRINTF(("axe_miibus_statchg: val=0x%x\n", val));
	err = axe_cmd(sc, AXE_CMD_WRITE_MEDIA, 0, val, NULL);
	if (err) {
		printf("%s: media change failed\n", sc->axe_dev.dv_xname);
		return;
	}
}

/*
 * Set media options.
 */
int
axe_ifmedia_upd(struct ifnet *ifp)
{
	struct axe_softc	*sc = ifp->if_softc;
	struct mii_data		*mii = GET_MII(sc);

	if (mii->mii_instance) {
		struct mii_softc	*miisc;
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	mii_mediachg(mii);

	return (0);
}

/*
 * Report current media status.
 */
void
axe_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct axe_softc	*sc = ifp->if_softc;
	struct mii_data		*mii = GET_MII(sc);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

void
axe_iff(struct axe_softc *sc)
{
	struct ifnet		*ifp = GET_IFP(sc);
	struct arpcom		*ac = &sc->arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	u_int32_t		h = 0;
	uWord			urxmode;
	u_int16_t		rxmode;
	u_int8_t		hashtbl[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	if (usbd_is_dying(sc->axe_udev))
		return;

	axe_cmd(sc, AXE_CMD_RXCTL_READ, 0, 0, urxmode);
	rxmode = UGETW(urxmode);
	rxmode &= ~(AXE_RXCMD_ALLMULTI | AXE_RXCMD_MULTICAST |
	    AXE_RXCMD_PROMISC);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept broadcast frames.
	 * Always accept frames destined to our station address.
	 */
	rxmode |= AXE_RXCMD_BROADCAST;
	if ((sc->axe_flags & (AX178 | AX772)) == 0)
		rxmode |= AXE_172_RXCMD_UNICAST;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		rxmode |= AXE_RXCMD_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxmode |= AXE_RXCMD_PROMISC;
	} else {
		rxmode |= AXE_RXCMD_MULTICAST;

		/* now program new ones */
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = ether_crc32_be(enm->enm_addrlo,
			    ETHER_ADDR_LEN) >> 26;

			hashtbl[h / 8] |= 1 << (h % 8);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	axe_cmd(sc, AXE_CMD_WRITE_MCAST, 0, 0, (void *)&hashtbl);
	axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, rxmode, NULL);
}

void
axe_reset(struct axe_softc *sc)
{
	if (usbd_is_dying(sc->axe_udev))
		return;
	/* XXX What to reset? */

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);
	return;
}

#define AXE_GPIO_WRITE(x,y) do {                                \
	axe_cmd(sc, AXE_CMD_WRITE_GPIO, 0, (x), NULL);          \
	usbd_delay_ms(sc->axe_udev, (y));			\
} while (0)

void
axe_ax88178_init(struct axe_softc *sc)
{
	int gpio0 = 0, phymode = 0, ledmode;
	u_int16_t eeprom, val;

	axe_cmd(sc, AXE_CMD_SROM_WR_ENABLE, 0, 0, NULL);
	/* XXX magic */
	axe_cmd(sc, AXE_CMD_SROM_READ, 0, 0x0017, &eeprom);
	axe_cmd(sc, AXE_CMD_SROM_WR_DISABLE, 0, 0, NULL);

	eeprom = letoh16(eeprom);

	DPRINTF((" EEPROM is 0x%x\n", eeprom));

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

	DPRINTF(("use gpio0: %d, phymode 0x%02x, eeprom 0x%04x\n",
	    gpio0, phymode, eeprom));

	/* power up external phy */
	AXE_GPIO_WRITE(AXE_GPIO1|AXE_GPIO1_EN | AXE_GPIO_RELOAD_EEPROM, 40);
	if (ledmode == 1) {
		AXE_GPIO_WRITE(AXE_GPIO1_EN, 30);
		AXE_GPIO_WRITE(AXE_GPIO1_EN | AXE_GPIO1, 30);
	} else {
		val = gpio0 == 1 ? AXE_GPIO0 | AXE_GPIO0_EN : 
	    	    AXE_GPIO1 | AXE_GPIO1_EN;
		AXE_GPIO_WRITE(val | AXE_GPIO2 | AXE_GPIO2_EN, 30);
		AXE_GPIO_WRITE(val | AXE_GPIO2_EN, 300);
		AXE_GPIO_WRITE(val | AXE_GPIO2 | AXE_GPIO2_EN, 30);
	}

	/* initialize phy */
	if (phymode == AXE_PHY_MODE_REALTEK_8211CL) {
		axe_miibus_writereg(&sc->axe_dev, sc->axe_phyno, 0x1f, 0x0005);
		axe_miibus_writereg(&sc->axe_dev, sc->axe_phyno, 0x0c, 0x0000);
		val = axe_miibus_readreg(&sc->axe_dev, sc->axe_phyno, 0x0001);
		axe_miibus_writereg(&sc->axe_dev, sc->axe_phyno, 0x01,
		    val | 0x0080);
		axe_miibus_writereg(&sc->axe_dev, sc->axe_phyno, 0x1f, 0x0000);
	}

	/* soft reset */
	axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0, AXE_SW_RESET_CLEAR, NULL);
	usbd_delay_ms(sc->axe_udev, 150);
	axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0,
	    AXE_SW_RESET_PRL | AXE_178_RESET_MAGIC, NULL);
	usbd_delay_ms(sc->axe_udev, 150);
	/* Enable MII/GMII/RGMII for external PHY */
	axe_cmd(sc, AXE_CMD_SW_PHY_SELECT, 0, 0, NULL);
	usbd_delay_ms(sc->axe_udev, 10);
	axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, 0, NULL);
}

/* Read Ethernet Address from EEPROM if it is zero */
void
axe_ax88772b_nodeid(struct axe_softc *sc, u_char *eaddr)
{
	int i;
	uint16_t val;

	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		if (eaddr[i] != 0)
			break;
	}

	/* We already have an ethernet address */
	if (i != ETHER_ADDR_LEN)
		return;

	/* read from EEPROM */
	for (i = 0; i < ETHER_ADDR_LEN/2; i++) {
		axe_cmd(sc, AXE_CMD_SROM_READ, 0, AXE_EEPROM_772B_NODEID + i, &val);
		val = ntohs(val);
		*eaddr++ = (u_char)((val >> 8) & 0xff);
		*eaddr++ = (u_char)(val & 0xff);
	}
}

void
axe_ax88772_init(struct axe_softc *sc)
{
	axe_cmd(sc, AXE_CMD_WRITE_GPIO, 0, 0x00b0, NULL);
	usbd_delay_ms(sc->axe_udev, 40);

	if (sc->axe_phyno == AXE_PHY_NO_AX772_EPHY) {
		/* ask for the embedded PHY */
		axe_cmd(sc, AXE_CMD_SW_PHY_SELECT, 0, 0x01, NULL);
		usbd_delay_ms(sc->axe_udev, 10);

		/* power down and reset state, pin reset state */
		axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0, AXE_SW_RESET_CLEAR, NULL);
		usbd_delay_ms(sc->axe_udev, 60);

		/* power down/reset state, pin operating state */
		axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0,
		    AXE_SW_RESET_IPPD | AXE_SW_RESET_PRL, NULL);
		usbd_delay_ms(sc->axe_udev, 150);

		/* power up, reset */
		axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0, AXE_SW_RESET_PRL, NULL);

		/* power up, operating */
		axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0,
		    AXE_SW_RESET_IPRL | AXE_SW_RESET_PRL, NULL);
	} else {
		/* ask for external PHY */
		axe_cmd(sc, AXE_CMD_SW_PHY_SELECT, 0, 0x00, NULL);
		usbd_delay_ms(sc->axe_udev, 10);

		/* power down internal PHY */
		axe_cmd(sc, AXE_CMD_SW_RESET_REG, 0,
		    AXE_SW_RESET_IPPD | AXE_SW_RESET_PRL, NULL);
	}

	usbd_delay_ms(sc->axe_udev, 150);
	axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, 0, NULL);
}

static int
axe_get_phyno(struct axe_softc *sc, int sel)
{
	int phyno = -1;

	switch (AXE_PHY_TYPE(sc->axe_phyaddrs[sel])) {
	case PHY_TYPE_100_HOME:
	case PHY_TYPE_GIG:
		phyno  = AXE_PHY_NO(sc->axe_phyaddrs[sel]);
		break;
	case PHY_TYPE_SPECIAL:
		/* FALLTHROUGH */
	case PHY_TYPE_RSVD:
		/* FALLTHROUGH */
	case PHY_TYPE_NON_SUP:
		/* FALLTHROUGH */
	default:
		break;
	}

	return (phyno);
}

/*
 * Probe for a AX88172 chip.
 */
int
axe_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return (UMATCH_NONE);

	return (axe_lookup(uaa->vendor, uaa->product) != NULL ?
		UMATCH_VENDOR_PRODUCT_CONF_IFACE : UMATCH_NONE);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
axe_attach(struct device *parent, struct device *self, void *aux)
{
	struct axe_softc *sc = (struct axe_softc *)self;
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	struct mii_data	*mii;
	u_char eaddr[ETHER_ADDR_LEN];
	char *devname = sc->axe_dev.dv_xname;
	struct ifnet *ifp;
	int i, s;

	sc->axe_unit = self->dv_unit; /*device_get_unit(self);*/
	sc->axe_udev = uaa->device;
	sc->axe_iface = uaa->iface;
	sc->axe_flags = axe_lookup(uaa->vendor, uaa->product)->axe_flags;

	usb_init_task(&sc->axe_tick_task, axe_tick_task, sc,
	    USB_TASK_TYPE_GENERIC);
	rw_init(&sc->axe_mii_lock, "axemii");
	usb_init_task(&sc->axe_stop_task, (void (*)(void *))axe_stop, sc,
	    USB_TASK_TYPE_GENERIC);

	sc->axe_product = uaa->product;
	sc->axe_vendor = uaa->vendor;

	id = usbd_get_interface_descriptor(sc->axe_iface);

	/* decide on what our bufsize will be */
	if (sc->axe_flags & (AX178 | AX772))
		sc->axe_bufsz = (sc->axe_udev->speed == USB_SPEED_HIGH) ?
		    AXE_178_MAX_BUFSZ : AXE_178_MIN_BUFSZ;
	else
		sc->axe_bufsz = AXE_172_BUFSZ;

	/* Find endpoints. */
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->axe_iface, i);
		if (!ed) {
			printf("%s: couldn't get ep %d\n",
			    sc->axe_dev.dv_xname, i);
			return;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->axe_ed[AXE_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->axe_ed[AXE_ENDPT_TX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->axe_ed[AXE_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	s = splnet();

	/* We need the PHYID for init dance in some cases */
	axe_cmd(sc, AXE_CMD_READ_PHYID, 0, 0, (void *)&sc->axe_phyaddrs);

	DPRINTF((" phyaddrs[0]: %x phyaddrs[1]: %x\n",
	    sc->axe_phyaddrs[0], sc->axe_phyaddrs[1]));

	sc->axe_phyno = axe_get_phyno(sc, AXE_PHY_SEL_PRI);
	if (sc->axe_phyno == -1)
		sc->axe_phyno = axe_get_phyno(sc, AXE_PHY_SEL_SEC);
	if (sc->axe_phyno == -1) {
		printf("%s:", sc->axe_dev.dv_xname);
		printf(" no valid PHY address found, assuming PHY address 0\n");
		sc->axe_phyno = 0;
	}

	DPRINTF((" get_phyno %d\n", sc->axe_phyno));

	if (sc->axe_flags & AX178)
		axe_ax88178_init(sc);
	else if (sc->axe_flags & AX772)
		axe_ax88772_init(sc);

	/*
	 * Get station address.
	 */
	if (sc->axe_flags & (AX178 | AX772))
		axe_cmd(sc, AXE_178_CMD_READ_NODEID, 0, 0, &eaddr);
	else
		axe_cmd(sc, AXE_172_CMD_READ_NODEID, 0, 0, &eaddr);

	if (sc->axe_flags & AX772B)
		axe_ax88772b_nodeid(sc, eaddr);

	/*
	 * Load IPG values
	 */
	axe_cmd(sc, AXE_CMD_READ_IPG012, 0, 0, (void *)&sc->axe_ipgs);

	/*
	 * An ASIX chip was detected. Inform the world.
	 */
	printf("%s:", sc->axe_dev.dv_xname);
	if (sc->axe_flags & AX178)
		printf(" AX88178");
	else if (sc->axe_flags & AX772B)
		printf(" AX88772B");
	else if (sc->axe_flags & AX772)
		printf(" AX88772");
	else
		printf(" AX88172");
	printf(", address %s\n", ether_sprintf(eaddr));

	bcopy(eaddr, (char *)&sc->arpcom.ac_enaddr, ETHER_ADDR_LEN);

	/* Initialize interface info.*/
	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	strlcpy(ifp->if_xname, devname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = axe_ioctl;
	ifp->if_start = axe_start;
	ifp->if_watchdog = axe_watchdog;

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/* Initialize MII/media info. */
	mii = &sc->axe_mii;
	mii->mii_ifp = ifp;
	mii->mii_readreg = axe_miibus_readreg;
	mii->mii_writereg = axe_miibus_writereg;
	mii->mii_statchg = axe_miibus_statchg;
	mii->mii_flags = MIIF_AUTOTSLEEP;

	ifmedia_init(&mii->mii_media, 0, axe_ifmedia_upd, axe_ifmedia_sts);
	mii_attach(self, mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY, 0);

	if (LIST_FIRST(&mii->mii_phys) == NULL) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_NONE, 0, NULL);
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_NONE);
	} else
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_AUTO);

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->axe_stat_ch, axe_tick, sc);

	splx(s);
}

int
axe_detach(struct device *self, int flags)
{
	struct axe_softc	*sc = (struct axe_softc *)self;
	int			s;
	struct ifnet		*ifp = GET_IFP(sc);

	DPRINTFN(2,("%s: %s: enter\n", sc->axe_dev.dv_xname, __func__));

	if (timeout_initialized(&sc->axe_stat_ch))
		timeout_del(&sc->axe_stat_ch);

	if (sc->axe_ep[AXE_ENDPT_TX] != NULL)
		usbd_abort_pipe(sc->axe_ep[AXE_ENDPT_TX]);
	if (sc->axe_ep[AXE_ENDPT_RX] != NULL)
		usbd_abort_pipe(sc->axe_ep[AXE_ENDPT_RX]);
	if (sc->axe_ep[AXE_ENDPT_INTR] != NULL)
		usbd_abort_pipe(sc->axe_ep[AXE_ENDPT_INTR]);

	/*
	 * Remove any pending tasks.  They cannot be executing because they run
	 * in the same thread as detach.
	 */
	usb_rem_task(sc->axe_udev, &sc->axe_tick_task);
	usb_rem_task(sc->axe_udev, &sc->axe_stop_task);

	s = splusb();

	if (--sc->axe_refcnt >= 0) {
		/* Wait for processes to go away */
		usb_detach_wait(&sc->axe_dev);
	}

	if (ifp->if_flags & IFF_RUNNING)
		axe_stop(sc);

	mii_detach(&sc->axe_mii, MII_PHY_ANY, MII_OFFSET_ANY);
	ifmedia_delete_instance(&sc->axe_mii.mii_media, IFM_INST_ANY);
	if (ifp->if_softc != NULL) {
		ether_ifdetach(ifp);
		if_detach(ifp);
	}

#ifdef DIAGNOSTIC
	if (sc->axe_ep[AXE_ENDPT_TX] != NULL ||
	    sc->axe_ep[AXE_ENDPT_RX] != NULL ||
	    sc->axe_ep[AXE_ENDPT_INTR] != NULL)
		printf("%s: detach has active endpoints\n",
		    sc->axe_dev.dv_xname);
#endif

	splx(s);

	return (0);
}

struct mbuf *
axe_newbuf(void)
{
	struct mbuf		*m;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (NULL);

	MCLGET(m, M_DONTWAIT);
	if (!(m->m_flags & M_EXT)) {
		m_freem(m);
		return (NULL);
	}

	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, ETHER_ALIGN);

	return (m);
}

int
axe_rx_list_init(struct axe_softc *sc)
{
	struct axe_cdata *cd;
	struct axe_chain *c;
	int i;

	DPRINTF(("%s: %s: enter\n", sc->axe_dev.dv_xname, __func__));

	cd = &sc->axe_cdata;
	for (i = 0; i < AXE_RX_LIST_CNT; i++) {
		c = &cd->axe_rx_chain[i];
		c->axe_sc = sc;
		c->axe_idx = i;
		c->axe_mbuf = NULL;
		if (c->axe_xfer == NULL) {
			c->axe_xfer = usbd_alloc_xfer(sc->axe_udev);
			if (c->axe_xfer == NULL)
				return (ENOBUFS);
			c->axe_buf = usbd_alloc_buffer(c->axe_xfer,
			    sc->axe_bufsz);
			if (c->axe_buf == NULL) {
				usbd_free_xfer(c->axe_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

int
axe_tx_list_init(struct axe_softc *sc)
{
	struct axe_cdata *cd;
	struct axe_chain *c;
	int i;

	DPRINTF(("%s: %s: enter\n", sc->axe_dev.dv_xname, __func__));

	cd = &sc->axe_cdata;
	for (i = 0; i < AXE_TX_LIST_CNT; i++) {
		c = &cd->axe_tx_chain[i];
		c->axe_sc = sc;
		c->axe_idx = i;
		c->axe_mbuf = NULL;
		if (c->axe_xfer == NULL) {
			c->axe_xfer = usbd_alloc_xfer(sc->axe_udev);
			if (c->axe_xfer == NULL)
				return (ENOBUFS);
			c->axe_buf = usbd_alloc_buffer(c->axe_xfer,
			    sc->axe_bufsz);
			if (c->axe_buf == NULL) {
				usbd_free_xfer(c->axe_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void
axe_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct axe_chain	*c = (struct axe_chain *)priv;
	struct axe_softc	*sc = c->axe_sc;
	struct ifnet		*ifp = GET_IFP(sc);
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
	u_char			*buf = c->axe_buf;
	u_int32_t		total_len;
	u_int16_t		pktlen = 0;
	struct mbuf		*m;
	struct axe_sframe_hdr	hdr;
	int			s;

	DPRINTFN(10,("%s: %s: enter\n", sc->axe_dev.dv_xname,__func__));

	if (usbd_is_dying(sc->axe_udev))
		return;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (usbd_ratecheck(&sc->axe_rx_notice)) {
			printf("%s: usb errors on rx: %s\n",
			    sc->axe_dev.dv_xname, usbd_errstr(status));
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->axe_ep[AXE_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	do {
		if (sc->axe_flags & (AX178 | AX772)) {
			if (total_len < sizeof(hdr)) {
				ifp->if_ierrors++;
				goto done;
			}

			buf += pktlen;

			memcpy(&hdr, buf, sizeof(hdr));
			total_len -= sizeof(hdr);

			if (((letoh16(hdr.len) & AXE_RH1M_RXLEN_MASK) ^
			    (letoh16(hdr.ilen) & AXE_RH1M_RXLEN_MASK)) !=
			    AXE_RH1M_RXLEN_MASK) {
				ifp->if_ierrors++;
				goto done;
			}
			pktlen = letoh16(hdr.len) & AXE_RH1M_RXLEN_MASK;
			if (pktlen > total_len) {
				ifp->if_ierrors++;
				goto done;
			}

			buf += sizeof(hdr);

			if ((pktlen % 2) != 0)
				pktlen++;

			if (total_len < pktlen)
				total_len = 0;
			else
				total_len -= pktlen;
		} else {
			pktlen = total_len; /* crc on the end? */
			total_len = 0;
		}

		m = axe_newbuf();
		if (m == NULL) {
			ifp->if_ierrors++;
			goto done;
		}

		m->m_pkthdr.len = m->m_len = pktlen;

		memcpy(mtod(m, char *), buf, pktlen);

		ml_enqueue(&ml, m);

	} while (total_len > 0);

done:
	/* push the packet up */
	s = splnet();
	if_input(ifp, &ml);
	splx(s);

	memset(c->axe_buf, 0, sc->axe_bufsz);

	/* Setup new transfer. */
	usbd_setup_xfer(xfer, sc->axe_ep[AXE_ENDPT_RX],
	    c, c->axe_buf, sc->axe_bufsz,
	    USBD_SHORT_XFER_OK | USBD_NO_COPY,
	    USBD_NO_TIMEOUT, axe_rxeof);
	usbd_transfer(xfer);

	DPRINTFN(10,("%s: %s: start rx\n", sc->axe_dev.dv_xname, __func__));

	return;
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

void
axe_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct axe_softc	*sc;
	struct axe_chain	*c;
	struct ifnet		*ifp;
	int			s;

	c = priv;
	sc = c->axe_sc;
	ifp = &sc->arpcom.ac_if;

	if (usbd_is_dying(sc->axe_udev))
		return;

	s = splnet();

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			splx(s);
			return;
		}
		ifp->if_oerrors++;
		printf("axe%d: usb error on tx: %s\n", sc->axe_unit,
		    usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->axe_ep[AXE_ENDPT_TX]);
		splx(s);
		return;
	}

	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

	m_freem(c->axe_mbuf);
	c->axe_mbuf = NULL;

	if (ifq_empty(&ifp->if_snd) == 0)
		axe_start(ifp);

	splx(s);
	return;
}

void
axe_tick(void *xsc)
{
	struct axe_softc *sc = xsc;

	if (sc == NULL)
		return;

	DPRINTFN(0xff, ("%s: %s: enter\n", sc->axe_dev.dv_xname,
			__func__));

	if (usbd_is_dying(sc->axe_udev))
		return;

	/* Perform periodic stuff in process context */
	usb_add_task(sc->axe_udev, &sc->axe_tick_task);

}

void
axe_tick_task(void *xsc)
{
	int			s;
	struct axe_softc	*sc;
	struct ifnet		*ifp;
	struct mii_data		*mii;

	sc = xsc;

	if (sc == NULL)
		return;

	if (usbd_is_dying(sc->axe_udev))
		return;

	ifp = GET_IFP(sc);
	mii = GET_MII(sc);
	if (mii == NULL)
		return;

	s = splnet();

	mii_tick(mii);
	if (sc->axe_link == 0)
		axe_miibus_statchg(&sc->axe_dev);
	timeout_add_sec(&sc->axe_stat_ch, 1);

	splx(s);
}

int
axe_encap(struct axe_softc *sc, struct mbuf *m, int idx)
{
	struct axe_chain	*c;
	usbd_status		err;
	struct axe_sframe_hdr	hdr;
	int			length, boundary;

	c = &sc->axe_cdata.axe_tx_chain[idx];

	if (sc->axe_flags & (AX178 | AX772)) {
		boundary = (sc->axe_udev->speed == USB_SPEED_HIGH) ? 512 : 64;

		hdr.len = htole16(m->m_pkthdr.len);
		hdr.ilen = ~hdr.len;

		memcpy(c->axe_buf, &hdr, sizeof(hdr));
		length = sizeof(hdr);

		m_copydata(m, 0, m->m_pkthdr.len, c->axe_buf + length);
		length += m->m_pkthdr.len;

		if ((length % boundary) == 0) {
			hdr.len = 0x0000;
			hdr.ilen = 0xffff;
			memcpy(c->axe_buf + length, &hdr, sizeof(hdr));
			length += sizeof(hdr);
		}

	} else {
		m_copydata(m, 0, m->m_pkthdr.len, c->axe_buf);
		length = m->m_pkthdr.len;
	}

	c->axe_mbuf = m;

	usbd_setup_xfer(c->axe_xfer, sc->axe_ep[AXE_ENDPT_TX],
	    c, c->axe_buf, length, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
	    10000, axe_txeof);

	/* Transmit */
	err = usbd_transfer(c->axe_xfer);
	if (err != USBD_IN_PROGRESS) {
		c->axe_mbuf = NULL;
		axe_stop(sc);
		return(EIO);
	}

	sc->axe_cdata.axe_tx_cnt++;

	return(0);
}

void
axe_start(struct ifnet *ifp)
{
	struct axe_softc	*sc;
	struct mbuf		*m_head = NULL;

	sc = ifp->if_softc;

	if (!sc->axe_link)
		return;

	if (ifq_is_oactive(&ifp->if_snd))
		return;

	m_head = ifq_dequeue(&ifp->if_snd);
	if (m_head == NULL)
		return;

	if (axe_encap(sc, m_head, 0)) {
		m_freem(m_head);
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

	/*
	 * If there's a BPF listener, bounce a copy of this frame
	 * to him.
	 */
#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif

	ifq_set_oactive(&ifp->if_snd);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;

	return;
}

void
axe_init(void *xsc)
{
	struct axe_softc	*sc = xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct axe_chain	*c;
	usbd_status		err;
	uWord			urxmode;
	int			rxmode;
	int			i, s;

	s = splnet();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	axe_reset(sc);

	/* set MAC address */
	if (sc->axe_flags & (AX178 | AX772))
		axe_cmd(sc, AXE_178_CMD_WRITE_NODEID, 0, 0,
		    &sc->arpcom.ac_enaddr);

	/* Enable RX logic. */

	/* Init RX ring. */
	if (axe_rx_list_init(sc) == ENOBUFS) {
		printf("axe%d: rx list init failed\n", sc->axe_unit);
		splx(s);
		return;
	}

	/* Init TX ring. */
	if (axe_tx_list_init(sc) == ENOBUFS) {
		printf("axe%d: tx list init failed\n", sc->axe_unit);
		splx(s);
		return;
	}

	/* Set transmitter IPG values */
	if (sc->axe_flags & (AX178 | AX772))
		axe_cmd(sc, AXE_178_CMD_WRITE_IPG012, sc->axe_ipgs[2],
		    (sc->axe_ipgs[1] << 8) | (sc->axe_ipgs[0]), NULL);
	else {
		axe_cmd(sc, AXE_172_CMD_WRITE_IPG0, 0, sc->axe_ipgs[0], NULL);
		axe_cmd(sc, AXE_172_CMD_WRITE_IPG1, 0, sc->axe_ipgs[1], NULL);
		axe_cmd(sc, AXE_172_CMD_WRITE_IPG2, 0, sc->axe_ipgs[2], NULL);
	}

	/* Program promiscuous mode and multicast filters. */
	axe_iff(sc);

	/* Enable receiver, set RX mode */
	axe_cmd(sc, AXE_CMD_RXCTL_READ, 0, 0, urxmode);
	rxmode = UGETW(urxmode);
	rxmode |= AXE_RXCMD_ENABLE;
	if (sc->axe_flags & AX772B)
		rxmode |= AXE_772B_RXCMD_RH1M;
	else if (sc->axe_flags & (AX178 | AX772)) {
		if (sc->axe_udev->speed == USB_SPEED_HIGH) {
			/* largest possible USB buffer size for AX88178 */
			rxmode |= AXE_178_RXCMD_MFB;
		}
	}
	axe_cmd(sc, AXE_CMD_RXCTL_WRITE, 0, rxmode, NULL);

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->axe_iface, sc->axe_ed[AXE_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->axe_ep[AXE_ENDPT_RX]);
	if (err) {
		printf("axe%d: open rx pipe failed: %s\n",
		    sc->axe_unit, usbd_errstr(err));
		splx(s);
		return;
	}

	err = usbd_open_pipe(sc->axe_iface, sc->axe_ed[AXE_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->axe_ep[AXE_ENDPT_TX]);
	if (err) {
		printf("axe%d: open tx pipe failed: %s\n",
		    sc->axe_unit, usbd_errstr(err));
		splx(s);
		return;
	}

	/* Start up the receive pipe. */
	for (i = 0; i < AXE_RX_LIST_CNT; i++) {
		c = &sc->axe_cdata.axe_rx_chain[i];
		usbd_setup_xfer(c->axe_xfer, sc->axe_ep[AXE_ENDPT_RX],
		    c, c->axe_buf, sc->axe_bufsz,
		    USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    USBD_NO_TIMEOUT, axe_rxeof);
		usbd_transfer(c->axe_xfer);
	}

	sc->axe_link = 0;
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	timeout_add_sec(&sc->axe_stat_ch, 1);
	return;
}

int
axe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct axe_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	int			s, error = 0;

	s = splnet();

	switch(cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			axe_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				axe_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				axe_stop(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->axe_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			axe_iff(sc);
		error = 0;
	}

	splx(s);
	return(error);
}

void
axe_watchdog(struct ifnet *ifp)
{
	struct axe_softc	*sc;
	struct axe_chain	*c;
	usbd_status		stat;
	int			s;

	sc = ifp->if_softc;

	ifp->if_oerrors++;
	printf("axe%d: watchdog timeout\n", sc->axe_unit);

	s = splusb();
	c = &sc->axe_cdata.axe_tx_chain[0];
	usbd_get_xfer_status(c->axe_xfer, NULL, NULL, NULL, &stat);
	axe_txeof(c->axe_xfer, c, stat);

	if (!ifq_empty(&ifp->if_snd))
		axe_start(ifp);
	splx(s);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
axe_stop(struct axe_softc *sc)
{
	usbd_status		err;
	struct ifnet		*ifp;
	int			i;

	axe_reset(sc);

	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_del(&sc->axe_stat_ch);

	/* Stop transfers. */
	if (sc->axe_ep[AXE_ENDPT_RX] != NULL) {
		err = usbd_close_pipe(sc->axe_ep[AXE_ENDPT_RX]);
		if (err) {
			printf("axe%d: close rx pipe failed: %s\n",
			    sc->axe_unit, usbd_errstr(err));
		}
		sc->axe_ep[AXE_ENDPT_RX] = NULL;
	}

	if (sc->axe_ep[AXE_ENDPT_TX] != NULL) {
		err = usbd_close_pipe(sc->axe_ep[AXE_ENDPT_TX]);
		if (err) {
			printf("axe%d: close tx pipe failed: %s\n",
			    sc->axe_unit, usbd_errstr(err));
		}
		sc->axe_ep[AXE_ENDPT_TX] = NULL;
	}

	if (sc->axe_ep[AXE_ENDPT_INTR] != NULL) {
		err = usbd_close_pipe(sc->axe_ep[AXE_ENDPT_INTR]);
		if (err) {
			printf("axe%d: close intr pipe failed: %s\n",
			    sc->axe_unit, usbd_errstr(err));
		}
		sc->axe_ep[AXE_ENDPT_INTR] = NULL;
	}

	/* Free RX resources. */
	for (i = 0; i < AXE_RX_LIST_CNT; i++) {
		if (sc->axe_cdata.axe_rx_chain[i].axe_mbuf != NULL) {
			m_freem(sc->axe_cdata.axe_rx_chain[i].axe_mbuf);
			sc->axe_cdata.axe_rx_chain[i].axe_mbuf = NULL;
		}
		if (sc->axe_cdata.axe_rx_chain[i].axe_xfer != NULL) {
			usbd_free_xfer(sc->axe_cdata.axe_rx_chain[i].axe_xfer);
			sc->axe_cdata.axe_rx_chain[i].axe_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < AXE_TX_LIST_CNT; i++) {
		if (sc->axe_cdata.axe_tx_chain[i].axe_mbuf != NULL) {
			m_freem(sc->axe_cdata.axe_tx_chain[i].axe_mbuf);
			sc->axe_cdata.axe_tx_chain[i].axe_mbuf = NULL;
		}
		if (sc->axe_cdata.axe_tx_chain[i].axe_xfer != NULL) {
			usbd_free_xfer(sc->axe_cdata.axe_tx_chain[i].axe_xfer);
			sc->axe_cdata.axe_tx_chain[i].axe_xfer = NULL;
		}
	}

	sc->axe_link = 0;
}

