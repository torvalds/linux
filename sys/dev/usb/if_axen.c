/*	$OpenBSD: if_axen.c,v 1.34 2024/10/07 07:35:40 kevlo Exp $	*/

/*
 * Copyright (c) 2013 Yojiro UO <yuo@openbsd.org>
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
 * ASIX Electronics AX88178a/AX88772d USB 2.0 ethernet and 
 * AX88179/AX88179a USB 3.0 Ethernet driver.
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

#include <dev/usb/if_axenreg.h>

#ifdef AXEN_DEBUG
#define DPRINTF(x)	do { if (axendebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (axendebug >= (n)) printf x; } while (0)
int	axendebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define AXEN_TOE	/* enable checksum offload function */

/*
 * Various supported device vendors/products.
 */
const struct axen_type axen_devs[] = {
#if 0 /* not tested */
	{ { USB_VENDOR_ASIX, USB_PRODUCT_ASIX_AX88178A}, AX178A },
#endif
	{ { USB_VENDOR_ASIX, USB_PRODUCT_ASIX_AX88179}, AX179 },
	{ { USB_VENDOR_DLINK, USB_PRODUCT_DLINK_DUB1312}, AX179 },
	{ { USB_VENDOR_LENOVO, USB_PRODUCT_LENOVO_AX88179}, AX179 },
	{ { USB_VENDOR_SAMSUNG2, USB_PRODUCT_SAMSUNG2_AX88179}, AX179 },
	{ { USB_VENDOR_SITECOMEU, USB_PRODUCT_SITECOMEU_LN032}, AX179 }
};

#define axen_lookup(v, p) ((struct axen_type *)usb_lookup(axen_devs, v, p))

int	axen_match(struct device *, void *, void *);
void	axen_attach(struct device *, struct device *, void *);
int	axen_detach(struct device *, int);

struct cfdriver axen_cd = {
	NULL, "axen", DV_IFNET
};

const struct cfattach axen_ca = {
	sizeof(struct axen_softc), axen_match, axen_attach, axen_detach
};

int	axen_tx_list_init(struct axen_softc *);
int	axen_rx_list_init(struct axen_softc *);
struct mbuf *axen_newbuf(void);
int	axen_encap(struct axen_softc *, struct mbuf *, int);
void	axen_rxeof(struct usbd_xfer *, void *, usbd_status);
void	axen_txeof(struct usbd_xfer *, void *, usbd_status);
void	axen_tick(void *);
void	axen_tick_task(void *);
void	axen_start(struct ifnet *);
int	axen_ioctl(struct ifnet *, u_long, caddr_t);
void	axen_init(void *);
void	axen_stop(struct axen_softc *);
void	axen_watchdog(struct ifnet *);
int	axen_miibus_readreg(struct device *, int, int);
void	axen_miibus_writereg(struct device *, int, int, int);
void	axen_miibus_statchg(struct device *);
int	axen_cmd(struct axen_softc *, int, int, int, void *);
int	axen_ifmedia_upd(struct ifnet *);
void	axen_ifmedia_sts(struct ifnet *, struct ifmediareq *);
void	axen_reset(struct axen_softc *sc);
void	axen_iff(struct axen_softc *);
void	axen_lock_mii(struct axen_softc *sc);
void	axen_unlock_mii(struct axen_softc *sc);

void	axen_ax88179_init(struct axen_softc *);

/* Get exclusive access to the MII registers */
void
axen_lock_mii(struct axen_softc *sc)
{
	sc->axen_refcnt++;
	rw_enter_write(&sc->axen_mii_lock);
}

void
axen_unlock_mii(struct axen_softc *sc)
{
	rw_exit_write(&sc->axen_mii_lock);
	if (--sc->axen_refcnt < 0)
		usb_detach_wakeup(&sc->axen_dev);
}

int
axen_cmd(struct axen_softc *sc, int cmd, int index, int val, void *buf)
{
	usb_device_request_t	req;
	usbd_status		err;

	if (usbd_is_dying(sc->axen_udev))
		return 0;

	if (AXEN_CMD_DIR(cmd))
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	else
		req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = AXEN_CMD_CMD(cmd);
	USETW(req.wValue, val);
	USETW(req.wIndex, index);
	USETW(req.wLength, AXEN_CMD_LEN(cmd));

	err = usbd_do_request(sc->axen_udev, &req, buf);
	DPRINTFN(5, ("axen_cmd: cmd 0x%04x val 0x%04x len %d\n",
	    cmd, val, AXEN_CMD_LEN(cmd)));

	if (err) {
		DPRINTF(("axen_cmd err: cmd: %d, error: %d\n", cmd, err));
		return -1;
	}

	return 0;
}

int
axen_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct axen_softc	*sc = (void *)dev;
	int			err;
	uWord			val;
	int			ival;

	if (usbd_is_dying(sc->axen_udev)) {
		DPRINTF(("axen: dying\n"));
		return 0;
	}

	if (sc->axen_phyno != phy)
		return 0;

	axen_lock_mii(sc);
	err = axen_cmd(sc, AXEN_CMD_MII_READ_REG, reg, phy, &val);
	axen_unlock_mii(sc);

	if (err) {
		printf("axen%d: read PHY failed\n", sc->axen_unit);
		return -1;
	}

	ival = UGETW(val);
	DPRINTFN(2,("axen_miibus_readreg: phy 0x%x reg 0x%x val 0x%x\n",
	    phy, reg, ival));

	if (reg == MII_BMSR) {
		ival &= ~BMSR_EXTCAP;
	}

	return ival;
}

void
axen_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct axen_softc	*sc = (void *)dev;
	int			err;
	uWord			uval;

	if (usbd_is_dying(sc->axen_udev))
		return;

	if (sc->axen_phyno != phy)
		return;

	USETW(uval, val);
	axen_lock_mii(sc);
	err = axen_cmd(sc, AXEN_CMD_MII_WRITE_REG, reg, phy, &uval);
	axen_unlock_mii(sc);
	DPRINTFN(2, ("axen_miibus_writereg: phy 0x%x reg 0x%x val 0x%0x\n",
	    phy, reg, val));

	if (err) {
		printf("axen%d: write PHY failed\n", sc->axen_unit);
		return;
	}
}

void
axen_miibus_statchg(struct device *dev)
{
	struct axen_softc	*sc = (void *)dev;
	struct mii_data		*mii = GET_MII(sc);
	struct ifnet		*ifp;
	int			err;
	uint16_t		val;
	uWord			wval;

	ifp = GET_IFP(sc);
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_flags & IFF_RUNNING) == 0)
		return;

	sc->axen_link = 0;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		    case IFM_10_T:
		    case IFM_100_TX:
			sc->axen_link++;
			break;
		    case IFM_1000_T:
			if ((sc->axen_flags & AX772D) != 0)
				break;
			sc->axen_link++;
			break;
		    default:
			break;
		}
	}

	/* Lost link, do nothing. */
	if (sc->axen_link == 0)
		return;

	val = 0;
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0)
		val |= AXEN_MEDIUM_FDX;

	val |= (AXEN_MEDIUM_RECV_EN | AXEN_MEDIUM_ALWAYS_ONE);
	val |= (AXEN_MEDIUM_RXFLOW_CTRL_EN | AXEN_MEDIUM_TXFLOW_CTRL_EN);

	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_1000_T:
		val |= AXEN_MEDIUM_GIGA | AXEN_MEDIUM_EN_125MHZ;
		break;
	case IFM_100_TX:
		val |= AXEN_MEDIUM_PS;
		break;
	case IFM_10_T:
		/* doesn't need to be handled */
		break;
	}

	DPRINTF(("axen_miibus_statchg: val=0x%x\n", val));
	USETW(wval, val);
	axen_lock_mii(sc);
	err = axen_cmd(sc, AXEN_CMD_MAC_WRITE2, 2, AXEN_MEDIUM_STATUS, &wval);
	axen_unlock_mii(sc);
	if (err) {
		printf("%s: media change failed\n", sc->axen_dev.dv_xname);
		return;
	}
}

/*
 * Set media options.
 */
int
axen_ifmedia_upd(struct ifnet *ifp)
{
	struct axen_softc	*sc = ifp->if_softc;
	struct mii_data		*mii = GET_MII(sc);
	int err;

	sc->axen_link = 0;

	if (mii->mii_instance) {
		struct mii_softc	*miisc;
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}

	err = mii_mediachg(mii);
	if (err == ENXIO)
		return 0;
	else
		return err;
}

/*
 * Report current media status.
 */
void
axen_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct axen_softc	*sc = ifp->if_softc;
	struct mii_data		*mii = GET_MII(sc);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

void
axen_iff(struct axen_softc *sc)
{
	struct ifnet		*ifp = GET_IFP(sc);
	struct arpcom		*ac = &sc->arpcom;
	struct ether_multi	*enm;
	struct ether_multistep	step;
	u_int32_t		h = 0;
	u_int16_t		rxmode;
	u_int8_t		hashtbl[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	uWord			wval;

	if (usbd_is_dying(sc->axen_udev))
		return;

	rxmode = 0;

	/* Enable receiver, set RX mode */
	axen_lock_mii(sc);
	axen_cmd(sc, AXEN_CMD_MAC_READ2, 2, AXEN_MAC_RXCTL, &wval);
	rxmode = UGETW(wval);
	rxmode &= ~(AXEN_RXCTL_ACPT_ALL_MCAST | AXEN_RXCTL_ACPT_PHY_MCAST |
		  AXEN_RXCTL_PROMISC);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept broadcast frames.
	 * Always accept frames destined to our station address.
	 */
	rxmode |= AXEN_RXCTL_ACPT_BCAST;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		rxmode |= AXEN_RXCTL_ACPT_ALL_MCAST | AXEN_RXCTL_ACPT_PHY_MCAST;
		if (ifp->if_flags & IFF_PROMISC)
			rxmode |= AXEN_RXCTL_PROMISC;
	} else {
		rxmode |= AXEN_RXCTL_ACPT_ALL_MCAST | AXEN_RXCTL_ACPT_PHY_MCAST;

		/* now program new ones */
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = ether_crc32_be(enm->enm_addrlo,
			    ETHER_ADDR_LEN) >> 26;
			hashtbl[h / 8] |= 1 << (h % 8);
			ETHER_NEXT_MULTI(step, enm);
		}
	}

	axen_cmd(sc, AXEN_CMD_MAC_WRITE_FILTER, 8, AXEN_FILTER_MULTI, 
	    (void *)&hashtbl);
	USETW(wval, rxmode);
	axen_cmd(sc, AXEN_CMD_MAC_WRITE2, 2, AXEN_MAC_RXCTL, &wval);
	axen_unlock_mii(sc);
}

void
axen_reset(struct axen_softc *sc)
{
	if (usbd_is_dying(sc->axen_udev))
		return;
	
	axen_ax88179_init(sc);

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);
	return;
}

void
axen_ax88179_init(struct axen_softc *sc)
{
	uWord		wval;
	uByte		val;
	u_int16_t 	ctl, temp;
	struct axen_qctrl qctrl;

	axen_lock_mii(sc);

	/* XXX: ? */
	axen_cmd(sc, AXEN_CMD_MAC_READ, 1, AXEN_UNK_05, &val);
	DPRINTFN(5, ("AXEN_CMD_MAC_READ(0x05): 0x%02x\n", val));

	/* check AX88179 version, UA1 / UA2 */
	axen_cmd(sc, AXEN_CMD_MAC_READ, 1, AXEN_GENERAL_STATUS, &val);
	/* UA1 */
	if (!(val & AXEN_GENERAL_STATUS_MASK)) {
		sc->axen_rev = AXEN_REV_UA1;
		DPRINTF(("AX88179 ver. UA1\n"));
	} else {
		sc->axen_rev = AXEN_REV_UA2;
		DPRINTF(("AX88179 ver. UA2\n"));
	}

	/* power up ethernet PHY */
	USETW(wval, 0);
	axen_cmd(sc, AXEN_CMD_MAC_WRITE2, 2, AXEN_PHYPWR_RSTCTL, &wval);

	USETW(wval, AXEN_PHYPWR_RSTCTL_IPRL);
	axen_cmd(sc, AXEN_CMD_MAC_WRITE2, 2, AXEN_PHYPWR_RSTCTL, &wval);
	usbd_delay_ms(sc->axen_udev, 200);

	/* set clock mode */
	val = AXEN_PHYCLK_ACS | AXEN_PHYCLK_BCS;
	axen_cmd(sc, AXEN_CMD_MAC_WRITE, 1, AXEN_PHYCLK, &val);
	usbd_delay_ms(sc->axen_udev, 100);

	/* set monitor mode (disable) */
	val = AXEN_MONITOR_NONE;
	axen_cmd(sc, AXEN_CMD_MAC_WRITE, 1, AXEN_MONITOR_MODE, &val);

	/* enable auto detach */
	axen_cmd(sc, AXEN_CMD_EEPROM_READ, 2, AXEN_EEPROM_STAT, &wval);
	temp = UGETW(wval);
	DPRINTFN(2,("EEPROM0x43 = 0x%04x\n", temp));
	if (!(temp == 0xffff) && !(temp & 0x0100)) {
		/* Enable auto detach bit */
		val = 0;
		axen_cmd(sc, AXEN_CMD_MAC_WRITE, 1, AXEN_PHYCLK, &val);
		val = AXEN_PHYCLK_ULR;
		axen_cmd(sc, AXEN_CMD_MAC_WRITE, 1, AXEN_PHYCLK, &val);
		usbd_delay_ms(sc->axen_udev, 100);

		axen_cmd(sc, AXEN_CMD_MAC_READ2, 2, AXEN_PHYPWR_RSTCTL, &wval);
		ctl = UGETW(wval);
		ctl |= AXEN_PHYPWR_RSTCTL_AUTODETACH;
		USETW(wval, ctl);
		axen_cmd(sc, AXEN_CMD_MAC_WRITE2, 2, AXEN_PHYPWR_RSTCTL, &wval);
		usbd_delay_ms(sc->axen_udev, 200);
		printf("%s: enable auto detach (0x%04x)\n",
		    sc->axen_dev.dv_xname, ctl);
	}

	/* bulkin queue setting */
	axen_cmd(sc, AXEN_CMD_MAC_READ, 1, AXEN_USB_UPLINK, &val);
	switch (val) {
	case AXEN_USB_FS:
		DPRINTF(("uplink: USB1.1\n"));
		qctrl.ctrl	= 0x07;
		qctrl.timer_low	= 0xcc;
		qctrl.timer_high= 0x4c;
		qctrl.bufsize	= AXEN_BUFSZ_LS - 1;
		qctrl.ifg	= 0x08;
		break;
	case AXEN_USB_HS:
		DPRINTF(("uplink: USB2.0\n"));
		qctrl.ctrl	= 0x07;
		qctrl.timer_low	= 0x02;
		qctrl.timer_high= 0xa0;
		qctrl.bufsize	= AXEN_BUFSZ_HS - 1;
		qctrl.ifg	= 0xff;
		break;
	case AXEN_USB_SS:
		DPRINTF(("uplink: USB3.0\n"));
		qctrl.ctrl	= 0x07;
		qctrl.timer_low	= 0x4f;
		qctrl.timer_high= 0x00;
		qctrl.bufsize	= AXEN_BUFSZ_SS - 1;
		qctrl.ifg	= 0xff;
		break;
	default:
		printf("%s: unknown uplink bus:0x%02x\n",
		    sc->axen_dev.dv_xname, val);
		axen_unlock_mii(sc);
		return;
	}
	axen_cmd(sc, AXEN_CMD_MAC_SET_RXSR, 5, AXEN_RX_BULKIN_QCTRL, &qctrl);

	/* Set MAC address. */
	axen_cmd(sc, AXEN_CMD_MAC_WRITE_ETHER, ETHER_ADDR_LEN,
	    AXEN_CMD_MAC_NODE_ID, &sc->arpcom.ac_enaddr);

	/*
	 * set buffer high/low watermark to pause/resume.
	 * write 2byte will set high/log simultaneous with AXEN_PAUSE_HIGH.
	 * XXX: what is the best value? OSX driver uses 0x3c-0x4c as LOW-HIGH
	 * watermark parameters.
	 */
	val = 0x34;
	axen_cmd(sc, AXEN_CMD_MAC_WRITE, 1, AXEN_PAUSE_LOW_WATERMARK, &val);
	val = 0x52;
	axen_cmd(sc, AXEN_CMD_MAC_WRITE, 1, AXEN_PAUSE_HIGH_WATERMARK, &val);

	/* Set RX/TX configuration. */
	/* Offloading enable */
#ifdef AXEN_TOE
	val = AXEN_RXCOE_IPv4 | AXEN_RXCOE_TCPv4 | AXEN_RXCOE_UDPv4 |
	      AXEN_RXCOE_TCPv6 | AXEN_RXCOE_UDPv6;
#else
	val = AXEN_RXCOE_OFF;
#endif
	axen_cmd(sc, AXEN_CMD_MAC_WRITE, 1, AXEN_RX_COE, &val);

#ifdef AXEN_TOE
	val = AXEN_TXCOE_IPv4 | AXEN_TXCOE_TCPv4 | AXEN_TXCOE_UDPv4 |
	      AXEN_TXCOE_TCPv6 | AXEN_TXCOE_UDPv6;
#else
	val = AXEN_TXCOE_OFF;
#endif
	axen_cmd(sc, AXEN_CMD_MAC_WRITE, 1, AXEN_TX_COE, &val);

	/* Set RX control register */
	ctl = AXEN_RXCTL_IPE | AXEN_RXCTL_DROPCRCERR | AXEN_RXCTL_AUTOB;
	ctl |= AXEN_RXCTL_ACPT_PHY_MCAST | AXEN_RXCTL_ACPT_ALL_MCAST;
	ctl |= AXEN_RXCTL_START;
	USETW(wval, ctl);
	axen_cmd(sc, AXEN_CMD_MAC_WRITE2, 2, AXEN_MAC_RXCTL, &wval);

	/* set monitor mode (enable) */
	val = AXEN_MONITOR_PMETYPE | AXEN_MONITOR_PMEPOL | AXEN_MONITOR_RWMP;
	axen_cmd(sc, AXEN_CMD_MAC_WRITE, 1, AXEN_MONITOR_MODE, &val);
	axen_cmd(sc, AXEN_CMD_MAC_READ, 1, AXEN_MONITOR_MODE, &val);
	DPRINTF(("axen: Monitor mode = 0x%02x\n", val));

	/* set medium type */
	ctl = AXEN_MEDIUM_GIGA | AXEN_MEDIUM_FDX | AXEN_MEDIUM_ALWAYS_ONE |
	      AXEN_MEDIUM_RXFLOW_CTRL_EN | AXEN_MEDIUM_TXFLOW_CTRL_EN;
	ctl |= AXEN_MEDIUM_RECV_EN;
	USETW(wval, ctl);
	DPRINTF(("axen: set to medium mode: 0x%04x\n", UGETW(wval)));
	axen_cmd(sc, AXEN_CMD_MAC_WRITE2, 2, AXEN_MEDIUM_STATUS, &wval);
	usbd_delay_ms(sc->axen_udev, 100);

	axen_cmd(sc, AXEN_CMD_MAC_READ2, 2, AXEN_MEDIUM_STATUS, &wval);
	DPRINTF(("axen: current medium mode: 0x%04x\n", UGETW(wval)));
	axen_unlock_mii(sc);

#if 0 /* XXX: TBD.... */
#define GMII_LED_ACTIVE		0x1a
#define GMII_PHY_PAGE_SEL	0x1e
#define GMII_PHY_PAGE_SEL	0x1f
#define GMII_PAGE_EXT		0x0007
	axen_miibus_writereg(&sc->axen_dev, sc->axen_phyno, GMII_PHY_PAGE_SEL,
	    GMII_PAGE_EXT);
	axen_miibus_writereg(&sc->axen_dev, sc->axen_phyno, GMII_PHY_PAGE,
	    0x002c);
#endif

#if 1 /* XXX: phy hack ? */
	axen_miibus_writereg(&sc->axen_dev, sc->axen_phyno, 0x1F, 0x0005);
	axen_miibus_writereg(&sc->axen_dev, sc->axen_phyno, 0x0C, 0x0000);
	val = axen_miibus_readreg(&sc->axen_dev, sc->axen_phyno, 0x0001);
	axen_miibus_writereg(&sc->axen_dev, sc->axen_phyno, 0x01,
	    val | 0x0080);
	axen_miibus_writereg(&sc->axen_dev, sc->axen_phyno, 0x1F, 0x0000);
#endif
}

int
axen_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return (UMATCH_NONE);

	return (axen_lookup(uaa->vendor, uaa->product) != NULL ?
		UMATCH_VENDOR_PRODUCT_CONF_IFACE : UMATCH_NONE);
}

void
axen_attach(struct device *parent, struct device *self, void *aux)
{
	struct axen_softc	*sc = (struct axen_softc *)self;
	struct usb_attach_arg	*uaa = aux;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	usb_device_descriptor_t *dd;
	struct mii_data		*mii;
	u_char			 eaddr[ETHER_ADDR_LEN];
	char			*devname = sc->axen_dev.dv_xname;
	struct ifnet		*ifp;
	int			 i, s;

	sc->axen_unit = self->dv_unit; /*device_get_unit(self);*/
	sc->axen_udev = uaa->device;
	sc->axen_iface = uaa->iface;
	sc->axen_flags = axen_lookup(uaa->vendor, uaa->product)->axen_flags;

	usb_init_task(&sc->axen_tick_task, axen_tick_task, sc,
	    USB_TASK_TYPE_GENERIC);
	rw_init(&sc->axen_mii_lock, "axenmii");
	usb_init_task(&sc->axen_stop_task, (void (*)(void *))axen_stop, sc,
	    USB_TASK_TYPE_GENERIC);

	sc->axen_product = uaa->product;
	sc->axen_vendor = uaa->vendor;

	id = usbd_get_interface_descriptor(sc->axen_iface);

	/* decide on what our bufsize will be */
	switch (sc->axen_udev->speed) {
	case USB_SPEED_FULL:
	    	sc->axen_bufsz = AXEN_BUFSZ_LS * 1024; 
		break;
	case USB_SPEED_HIGH:
	    	sc->axen_bufsz = AXEN_BUFSZ_HS * 1024; 
		break;
	case USB_SPEED_SUPER:
	    	sc->axen_bufsz = AXEN_BUFSZ_SS * 1024; 
		break;
	default:
		printf("%s: not supported usb bus type", sc->axen_dev.dv_xname);
		return;
	}
		
	/* Find endpoints. */
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->axen_iface, i);
		if (!ed) {
			printf("%s: couldn't get ep %d\n",
			    sc->axen_dev.dv_xname, i);
			return;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->axen_ed[AXEN_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->axen_ed[AXEN_ENDPT_TX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->axen_ed[AXEN_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	dd = usbd_get_device_descriptor(sc->axen_udev);
	switch (UGETW(dd->bcdDevice)) {
	case 0x200:
		sc->axen_flags = AX179A;
		break;
	case 0x300:
		sc->axen_flags = AX772D;
		break;
	}

	s = splnet();

	sc->axen_phyno = AXEN_PHY_ID;
	DPRINTF((" get_phyno %d\n", sc->axen_phyno));

	/*
	 * Get station address.
	 */
	/* use MAC command */
	axen_lock_mii(sc);
	axen_cmd(sc, AXEN_CMD_MAC_READ_ETHER, ETHER_ADDR_LEN,
	    AXEN_CMD_MAC_NODE_ID, &eaddr);
	axen_unlock_mii(sc);

	/*
	 * An ASIX chip was detected. Inform the world.
	 */
	printf("%s:", sc->axen_dev.dv_xname);
	if (sc->axen_flags & AX178A)
		printf(" AX88178a");
	else if (sc->axen_flags & AX179)
		printf(" AX88179");
	else if (sc->axen_flags & AX772D)
		printf(" AX88772D");
	else
		printf(" AX88179A");
	printf(", address %s\n", ether_sprintf(eaddr));

	bcopy(eaddr, (char *)&sc->arpcom.ac_enaddr, ETHER_ADDR_LEN);

	axen_ax88179_init(sc);

	/* Initialize interface info. */
	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	strlcpy(ifp->if_xname, devname, IFNAMSIZ);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = axen_ioctl;
	ifp->if_start = axen_start;
	ifp->if_watchdog = axen_watchdog;

	ifp->if_capabilities = IFCAP_VLAN_MTU;
#ifdef AXEN_TOE
	ifp->if_capabilities |= IFCAP_CSUM_IPv4 | IFCAP_CSUM_TCPv4 |
	    IFCAP_CSUM_UDPv4 | IFCAP_CSUM_TCPv6 | IFCAP_CSUM_UDPv6;
#endif

	/* Initialize MII/media info. */
	mii = &sc->axen_mii;
	mii->mii_ifp = ifp;
	mii->mii_readreg = axen_miibus_readreg;
	mii->mii_writereg = axen_miibus_writereg;
	mii->mii_statchg = axen_miibus_statchg;
	mii->mii_flags = MIIF_AUTOTSLEEP;

	ifmedia_init(&mii->mii_media, 0, axen_ifmedia_upd, axen_ifmedia_sts);
	mii_attach(self, mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY, 0);

	if (LIST_FIRST(&mii->mii_phys) == NULL) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_NONE, 0, NULL);
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_NONE);
	} else
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_AUTO);

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->axen_stat_ch, axen_tick, sc);

	splx(s);
}

int
axen_detach(struct device *self, int flags)
{
	struct axen_softc	*sc = (struct axen_softc *)self;
	int			s;
	struct ifnet		*ifp = GET_IFP(sc);

	DPRINTFN(2,("%s: %s: enter\n", sc->axen_dev.dv_xname, __func__));

	if (timeout_initialized(&sc->axen_stat_ch))
		timeout_del(&sc->axen_stat_ch);

	if (sc->axen_ep[AXEN_ENDPT_TX] != NULL)
		usbd_abort_pipe(sc->axen_ep[AXEN_ENDPT_TX]);
	if (sc->axen_ep[AXEN_ENDPT_RX] != NULL)
		usbd_abort_pipe(sc->axen_ep[AXEN_ENDPT_RX]);
	if (sc->axen_ep[AXEN_ENDPT_INTR] != NULL)
		usbd_abort_pipe(sc->axen_ep[AXEN_ENDPT_INTR]);

	/*
	 * Remove any pending tasks.  They cannot be executing because they run
	 * in the same thread as detach.
	 */
	usb_rem_task(sc->axen_udev, &sc->axen_tick_task);
	usb_rem_task(sc->axen_udev, &sc->axen_stop_task);

	s = splusb();

	if (--sc->axen_refcnt >= 0) {
		/* Wait for processes to go away */
		usb_detach_wait(&sc->axen_dev);
	}

	if (ifp->if_flags & IFF_RUNNING)
		axen_stop(sc);

	mii_detach(&sc->axen_mii, MII_PHY_ANY, MII_OFFSET_ANY);
	ifmedia_delete_instance(&sc->axen_mii.mii_media, IFM_INST_ANY);
	if (ifp->if_softc != NULL) {
		ether_ifdetach(ifp);
		if_detach(ifp);
	}

#ifdef DIAGNOSTIC
	if (sc->axen_ep[AXEN_ENDPT_TX] != NULL ||
	    sc->axen_ep[AXEN_ENDPT_RX] != NULL ||
	    sc->axen_ep[AXEN_ENDPT_INTR] != NULL)
		printf("%s: detach has active endpoints\n",
		    sc->axen_dev.dv_xname);
#endif

	splx(s);

	return 0;
}

struct mbuf *
axen_newbuf(void)
{
	struct mbuf *m;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return NULL;

	MCLGET(m, M_DONTWAIT);
	if (!(m->m_flags & M_EXT)) {
		m_freem(m);
		return NULL;
	}

	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, ETHER_ALIGN);

	return m;
}

int
axen_rx_list_init(struct axen_softc *sc)
{
	struct axen_cdata *cd;
	struct axen_chain *c;
	int i;

	DPRINTF(("%s: %s: enter\n", sc->axen_dev.dv_xname, __func__));

	cd = &sc->axen_cdata;
	for (i = 0; i < AXEN_RX_LIST_CNT; i++) {
		c = &cd->axen_rx_chain[i];
		c->axen_sc = sc;
		c->axen_idx = i;
		c->axen_mbuf = NULL;
		if (c->axen_xfer == NULL) {
			c->axen_xfer = usbd_alloc_xfer(sc->axen_udev);
			if (c->axen_xfer == NULL)
				return ENOBUFS;
			c->axen_buf = usbd_alloc_buffer(c->axen_xfer,
			    sc->axen_bufsz);
			if (c->axen_buf == NULL) {
				usbd_free_xfer(c->axen_xfer);
				return ENOBUFS;
			}
		}
	}

	return 0;
}

int
axen_tx_list_init(struct axen_softc *sc)
{
	struct axen_cdata *cd;
	struct axen_chain *c;
	int i;

	DPRINTF(("%s: %s: enter\n", sc->axen_dev.dv_xname, __func__));

	cd = &sc->axen_cdata;
	for (i = 0; i < AXEN_TX_LIST_CNT; i++) {
		c = &cd->axen_tx_chain[i];
		c->axen_sc = sc;
		c->axen_idx = i;
		c->axen_mbuf = NULL;
		if (c->axen_xfer == NULL) {
			c->axen_xfer = usbd_alloc_xfer(sc->axen_udev);
			if (c->axen_xfer == NULL)
				return ENOBUFS;
			c->axen_buf = usbd_alloc_buffer(c->axen_xfer,
			    sc->axen_bufsz);
			if (c->axen_buf == NULL) {
				usbd_free_xfer(c->axen_xfer);
				return ENOBUFS;
			}
		}
	}

	return 0;
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void
axen_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct axen_chain	*c = (struct axen_chain *)priv;
	struct axen_softc	*sc = c->axen_sc;
	struct ifnet		*ifp = GET_IFP(sc);
	u_char			*buf = c->axen_buf;
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
	struct mbuf		*m;
	u_int32_t		total_len;
	u_int32_t		rx_hdr, pkt_hdr;
	u_int32_t		*hdr_p;
	u_int16_t		hdr_offset, pkt_count;
	size_t			pkt_len;
	size_t			temp;
	int			padlen, s;

	DPRINTFN(10,("%s: %s: enter\n", sc->axen_dev.dv_xname,__func__));

	if (usbd_is_dying(sc->axen_udev))
		return;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (usbd_ratecheck(&sc->axen_rx_notice)) {
			printf("%s: usb errors on rx: %s\n",
			    sc->axen_dev.dv_xname, usbd_errstr(status));
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->axen_ep[AXEN_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	if (total_len < sizeof(pkt_hdr)) {
		ifp->if_ierrors++;
		goto done;
	}

	/* 
	 * buffer map
	 *
	 * for ax88179
	 * [packet #0]...[packet #n][pkt hdr#0]..[pkt hdr#n][recv_hdr]
	 *
	 * for ax88179a
	 * [packet #0]...[packet #n][pkt hdr#0][dummy_hdr]..
	 * [pkt hdr#n][dummy_hdr][recv_hdr]
	 *
	 * each packet has 0xeeee as pseudo header..
	 */
	hdr_p = (u_int32_t *)(buf + total_len - sizeof(u_int32_t));
	rx_hdr = letoh32(*hdr_p);
	hdr_offset = (u_int16_t)(rx_hdr >> 16);
	pkt_count  = (u_int16_t)(rx_hdr & 0xffff);

	if (total_len > sc->axen_bufsz) {
		printf("%s: rxeof: too large transfer\n",
		    sc->axen_dev.dv_xname);
		goto done;
	}

	/* sanity check */
	if (hdr_offset > total_len) {
		ifp->if_ierrors++;
		goto done;
	}

	/* point first packet header */
	hdr_p = (u_int32_t*)(buf + hdr_offset);

	/*
	 * ax88179 will pack multiple ip packet to a USB transaction.
	 * process all of packets in the buffer
	 */

#if 1 /* XXX: paranoiac check. need to remove later */
#define AXEN_MAX_PACKED_PACKET 200 
	if (pkt_count > AXEN_MAX_PACKED_PACKET) {
		DPRINTF(("Too many packets (%d) in a transaction, discard.\n", 
		    pkt_count));
		goto done;
	}
#endif

	/* skip pseudo header (2byte) */
	padlen = 2;
	/* skip trailer padding (4Byte) for ax88179 */
	if (!(sc->axen_flags & (AX179A | AX772D)))
		padlen += 4;

	do {
		pkt_hdr = letoh32(*hdr_p);
		pkt_len = (pkt_hdr >> 16) & 0x1fff;

		DPRINTFN(10,("rxeof: packet#%d, pkt_hdr 0x%08x, pkt_len %zu\n",
		   pkt_count, pkt_hdr, pkt_len));

		/* skip dummy packet header */
		if (pkt_len == 0)
			goto nextpkt;

		if ((buf[0] != 0xee) || (buf[1] != 0xee)){
			printf("%s: invalid buffer(pkt#%d), continue\n",
			    sc->axen_dev.dv_xname, pkt_count);
	    		ifp->if_ierrors += pkt_count;
			goto done;
		}

		if ((pkt_hdr & AXEN_RXHDR_CRC_ERR) ||
	    	    (pkt_hdr & AXEN_RXHDR_DROP_ERR)) {
	    		ifp->if_ierrors++;
			/* move to next pkt header */
			DPRINTF(("crc err(pkt#%d)\n", pkt_count));
			goto nextpkt;
		}

		/* process each packet */
		/* allocate mbuf */
		m = axen_newbuf();
		if (m == NULL) {
			ifp->if_ierrors++;
			goto nextpkt;
		}

		m->m_pkthdr.len = m->m_len = pkt_len - padlen;

#ifdef AXEN_TOE
		/* checksum err */
		if ((pkt_hdr & AXEN_RXHDR_L3CSUM_ERR) || 
		    (pkt_hdr & AXEN_RXHDR_L4CSUM_ERR)) {
			printf("%s: checksum err (pkt#%d)\n",
			    sc->axen_dev.dv_xname, pkt_count);
			goto nextpkt;
		} else {
			m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;
		}

		int l4_type;
		l4_type = (pkt_hdr & AXEN_RXHDR_L4_TYPE_MASK) >> 
		    AXEN_RXHDR_L4_TYPE_OFFSET;

		if ((l4_type == AXEN_RXHDR_L4_TYPE_TCP) ||
		    (l4_type == AXEN_RXHDR_L4_TYPE_UDP)) 
			m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK |
			    M_UDP_CSUM_IN_OK;
#endif

		memcpy(mtod(m, char *), buf + 2, pkt_len - padlen);

		ml_enqueue(&ml, m);

nextpkt:
		/*
		 * prepare next packet 
		 * as each packet will be aligned 8byte boundary,
		 * need to fix up the start point of the buffer.
		 */
		temp = ((pkt_len + 7) & 0xfff8);
		buf = buf + temp;
		hdr_p++;
		pkt_count--;
	} while( pkt_count > 0);

done:
	/* push the packet up */
	s = splnet();
	if_input(ifp, &ml);
	splx(s);

	/* clear buffer for next transaction */
	memset(c->axen_buf, 0, sc->axen_bufsz);

	/* Setup new transfer. */
	usbd_setup_xfer(xfer, sc->axen_ep[AXEN_ENDPT_RX],
	    c, c->axen_buf, sc->axen_bufsz,
	    USBD_SHORT_XFER_OK | USBD_NO_COPY,
	    USBD_NO_TIMEOUT, axen_rxeof);
	usbd_transfer(xfer);

	DPRINTFN(10,("%s: %s: start rx\n", sc->axen_dev.dv_xname, __func__));
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
void
axen_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct axen_softc	*sc;
	struct axen_chain	*c;
	struct ifnet		*ifp;
	int			s;

	c = priv;
	sc = c->axen_sc;
	ifp = &sc->arpcom.ac_if;

	if (usbd_is_dying(sc->axen_udev))
		return;

	s = splnet();

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			splx(s);
			return;
		}
		ifp->if_oerrors++;
		printf("axen%d: usb error on tx: %s\n", sc->axen_unit,
		    usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->axen_ep[AXEN_ENDPT_TX]);
		splx(s);
		return;
	}

	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

	m_freem(c->axen_mbuf);
	c->axen_mbuf = NULL;

	if (ifq_empty(&ifp->if_snd) == 0)
		axen_start(ifp);

	splx(s);
}

void
axen_tick(void *xsc)
{
	struct axen_softc *sc = xsc;

	if (sc == NULL)
		return;

	DPRINTFN(0xff, ("%s: %s: enter\n", sc->axen_dev.dv_xname,
			__func__));

	if (usbd_is_dying(sc->axen_udev))
		return;

	/* Perform periodic stuff in process context */
	usb_add_task(sc->axen_udev, &sc->axen_tick_task);
}

void
axen_tick_task(void *xsc)
{
	int			s;
	struct axen_softc	*sc;
	struct mii_data		*mii;

	sc = xsc;

	if (sc == NULL)
		return;

	if (usbd_is_dying(sc->axen_udev))
		return;

	mii = GET_MII(sc);
	if (mii == NULL)
		return;

	s = splnet();

	mii_tick(mii);
	if (sc->axen_link == 0)
		axen_miibus_statchg(&sc->axen_dev);
	timeout_add_sec(&sc->axen_stat_ch, 1);

	splx(s);
}

int
axen_encap(struct axen_softc *sc, struct mbuf *m, int idx)
{
	struct axen_chain	*c;
	usbd_status		err;
	struct axen_sframe_hdr	hdr;
	int			length, boundary;

	c = &sc->axen_cdata.axen_tx_chain[idx];

	switch (sc->axen_udev->speed) {
	case USB_SPEED_FULL:
	    	boundary = 64;
		break;
	case USB_SPEED_HIGH:
	    	boundary = 512;
		break;
	case USB_SPEED_SUPER:
	    	boundary = 4096; /* XXX */
		break;
	default:
		printf("%s: not supported usb bus type", sc->axen_dev.dv_xname);
		return EIO;
	}

	hdr.plen = htole32(m->m_pkthdr.len);
	hdr.gso = 0; /* disable segmentation offloading */

	memcpy(c->axen_buf, &hdr, sizeof(hdr));
	length = sizeof(hdr);

	m_copydata(m, 0, m->m_pkthdr.len, c->axen_buf + length);
	length += m->m_pkthdr.len;

	if ((length % boundary) == 0) {
		hdr.plen = 0x0;
		hdr.gso |= 0x80008000;  /* enable padding */
		memcpy(c->axen_buf + length, &hdr, sizeof(hdr));
		length += sizeof(hdr);
	}

	c->axen_mbuf = m;

	usbd_setup_xfer(c->axen_xfer, sc->axen_ep[AXEN_ENDPT_TX],
	    c, c->axen_buf, length, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
	    10000, axen_txeof);

	/* Transmit */
	err = usbd_transfer(c->axen_xfer);
	if (err != USBD_IN_PROGRESS) {
		c->axen_mbuf = NULL;
		axen_stop(sc);
		return EIO;
	}

	sc->axen_cdata.axen_tx_cnt++;

	return 0;
}

void
axen_start(struct ifnet *ifp)
{
	struct axen_softc	*sc;
	struct mbuf		*m_head = NULL;

	sc = ifp->if_softc;

	if (!sc->axen_link)
		return;

	if (ifq_is_oactive(&ifp->if_snd))
		return;

	m_head = ifq_dequeue(&ifp->if_snd);
	if (m_head == NULL)
		return;

	if (axen_encap(sc, m_head, 0)) {
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
}

void
axen_init(void *xsc)
{
	struct axen_softc	*sc = xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct axen_chain	*c;
	usbd_status		err;
	int			i, s;
	uByte			bval;
	uWord			wval;
	uint16_t		rxmode;

	s = splnet();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	axen_reset(sc);

	/* XXX: ? */
	bval = 0x01;
	axen_lock_mii(sc);
	axen_cmd(sc, AXEN_CMD_MAC_WRITE, 1, AXEN_UNK_28, &bval);
	axen_unlock_mii(sc);

	/* Init RX ring. */
	if (axen_rx_list_init(sc) == ENOBUFS) {
		printf("axen%d: rx list init failed\n", sc->axen_unit);
		splx(s);
		return;
	}

	/* Init TX ring. */
	if (axen_tx_list_init(sc) == ENOBUFS) {
		printf("axen%d: tx list init failed\n", sc->axen_unit);
		splx(s);
		return;
	}

	/* Program promiscuous mode and multicast filters. */
	axen_iff(sc);

	/* Enable receiver, set RX mode */
	axen_lock_mii(sc);
	axen_cmd(sc, AXEN_CMD_MAC_READ2, 2, AXEN_MAC_RXCTL, &wval);
	rxmode = UGETW(wval);
	rxmode |= AXEN_RXCTL_START;
	USETW(wval, rxmode);
	axen_cmd(sc, AXEN_CMD_MAC_WRITE2, 2, AXEN_MAC_RXCTL, &wval);
	axen_unlock_mii(sc);

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->axen_iface, sc->axen_ed[AXEN_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->axen_ep[AXEN_ENDPT_RX]);
	if (err) {
		printf("axen%d: open rx pipe failed: %s\n",
		    sc->axen_unit, usbd_errstr(err));
		splx(s);
		return;
	}

	err = usbd_open_pipe(sc->axen_iface, sc->axen_ed[AXEN_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->axen_ep[AXEN_ENDPT_TX]);
	if (err) {
		printf("axen%d: open tx pipe failed: %s\n",
		    sc->axen_unit, usbd_errstr(err));
		splx(s);
		return;
	}

	/* Start up the receive pipe. */
	for (i = 0; i < AXEN_RX_LIST_CNT; i++) {
		c = &sc->axen_cdata.axen_rx_chain[i];
		usbd_setup_xfer(c->axen_xfer, sc->axen_ep[AXEN_ENDPT_RX],
		    c, c->axen_buf, sc->axen_bufsz,
		    USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    USBD_NO_TIMEOUT, axen_rxeof);
		usbd_transfer(c->axen_xfer);
	}

	sc->axen_link = 0;
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	timeout_add_sec(&sc->axen_stat_ch, 1);
	return;
}

int
axen_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct axen_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	int			s;
	int			error = 0;

	s = splnet();

	switch(cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			axen_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				axen_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				axen_stop(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->axen_mii.mii_media, cmd);
		break;

#if 0
	case SCIOCSIFMTU:
		/* XXX need to set AX_MEDIUM_JUMBO_EN here? */
		/* fall through */
#endif
	default:
		error = ether_ioctl(ifp, &sc->arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			axen_iff(sc);
		error = 0;
	}

	splx(s);

	return error;
}

void
axen_watchdog(struct ifnet *ifp)
{
	struct axen_softc	*sc;
	struct axen_chain	*c;
	usbd_status		stat;
	int			s;

	sc = ifp->if_softc;

	ifp->if_oerrors++;
	printf("axen%d: watchdog timeout\n", sc->axen_unit);

	s = splusb();
	c = &sc->axen_cdata.axen_tx_chain[0];
	usbd_get_xfer_status(c->axen_xfer, NULL, NULL, NULL, &stat);
	axen_txeof(c->axen_xfer, c, stat);

	if (!ifq_empty(&ifp->if_snd))
		axen_start(ifp);
	splx(s);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
axen_stop(struct axen_softc *sc)
{
	usbd_status		err;
	struct ifnet		*ifp;
	int			i;

	axen_reset(sc);

	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_del(&sc->axen_stat_ch);

	/* Stop transfers. */
	if (sc->axen_ep[AXEN_ENDPT_RX] != NULL) {
		err = usbd_close_pipe(sc->axen_ep[AXEN_ENDPT_RX]);
		if (err) {
			printf("axen%d: close rx pipe failed: %s\n",
			    sc->axen_unit, usbd_errstr(err));
		}
		sc->axen_ep[AXEN_ENDPT_RX] = NULL;
	}

	if (sc->axen_ep[AXEN_ENDPT_TX] != NULL) {
		err = usbd_close_pipe(sc->axen_ep[AXEN_ENDPT_TX]);
		if (err) {
			printf("axen%d: close tx pipe failed: %s\n",
			    sc->axen_unit, usbd_errstr(err));
		}
		sc->axen_ep[AXEN_ENDPT_TX] = NULL;
	}

	if (sc->axen_ep[AXEN_ENDPT_INTR] != NULL) {
		err = usbd_close_pipe(sc->axen_ep[AXEN_ENDPT_INTR]);
		if (err) {
			printf("axen%d: close intr pipe failed: %s\n",
			    sc->axen_unit, usbd_errstr(err));
		}
		sc->axen_ep[AXEN_ENDPT_INTR] = NULL;
	}

	/* Free RX resources. */
	for (i = 0; i < AXEN_RX_LIST_CNT; i++) {
		if (sc->axen_cdata.axen_rx_chain[i].axen_mbuf != NULL) {
			m_freem(sc->axen_cdata.axen_rx_chain[i].axen_mbuf);
			sc->axen_cdata.axen_rx_chain[i].axen_mbuf = NULL;
		}
		if (sc->axen_cdata.axen_rx_chain[i].axen_xfer != NULL) {
			usbd_free_xfer(sc->axen_cdata.axen_rx_chain[i].axen_xfer);
			sc->axen_cdata.axen_rx_chain[i].axen_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < AXEN_TX_LIST_CNT; i++) {

		if (sc->axen_cdata.axen_tx_chain[i].axen_mbuf != NULL) {
			m_freem(sc->axen_cdata.axen_tx_chain[i].axen_mbuf);
			sc->axen_cdata.axen_tx_chain[i].axen_mbuf = NULL;
		}
		if (sc->axen_cdata.axen_tx_chain[i].axen_xfer != NULL) {
			usbd_free_xfer(sc->axen_cdata.axen_tx_chain[i].axen_xfer);
			sc->axen_cdata.axen_tx_chain[i].axen_xfer = NULL;
		}
	}

	sc->axen_link = 0;
}
