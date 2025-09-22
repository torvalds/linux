/*	$OpenBSD: if_mos.c,v 1.44 2024/05/23 03:21:08 jsg Exp $	*/

/*
 * Copyright (c) 2008 Johann Christian Rode <jcrode@gmx.net>
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
 * Moschip MCS7730/MCS7830/MCS7832 USB to Ethernet controller 
 * The datasheet is available at the following URL: 
 * http://www.moschip.com/data/products/MCS7830/Data%20Sheet_7830.pdf
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

#include <dev/mii/miivar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/if_mosreg.h>

#ifdef MOS_DEBUG
#define DPRINTF(x)      do { if (mosdebug) printf x; } while (0)
#define DPRINTFN(n,x)   do { if (mosdebug >= (n)) printf x; } while (0)
int     mosdebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

/*
 * Various supported device vendors/products.
 */
const struct mos_type mos_devs[] = {
	{ { USB_VENDOR_MOSCHIP, USB_PRODUCT_MOSCHIP_MCS7730 }, MCS7730 },
	{ { USB_VENDOR_MOSCHIP, USB_PRODUCT_MOSCHIP_MCS7830 }, MCS7830 },
	{ { USB_VENDOR_MOSCHIP, USB_PRODUCT_MOSCHIP_MCS7832 }, MCS7832 },
	{ { USB_VENDOR_SITECOMEU, USB_PRODUCT_SITECOMEU_LN030 }, MCS7830 },
};
#define mos_lookup(v, p) ((struct mos_type *)usb_lookup(mos_devs, v, p))

int mos_match(struct device *, void *, void *);
void mos_attach(struct device *, struct device *, void *);
int mos_detach(struct device *, int);

struct cfdriver mos_cd = {
	NULL, "mos", DV_IFNET
};

const struct cfattach mos_ca = {
	sizeof(struct mos_softc), mos_match, mos_attach, mos_detach
};

int mos_tx_list_init(struct mos_softc *);
int mos_rx_list_init(struct mos_softc *);
struct mbuf *mos_newbuf(void);
int mos_encap(struct mos_softc *, struct mbuf *, int);
void mos_rxeof(struct usbd_xfer *, void *, usbd_status);
void mos_txeof(struct usbd_xfer *, void *, usbd_status);
void mos_tick(void *);
void mos_tick_task(void *);
void mos_start(struct ifnet *);
int mos_ioctl(struct ifnet *, u_long, caddr_t);
void mos_init(void *);
void mos_chip_init(struct mos_softc *);
void mos_stop(struct mos_softc *);
void mos_watchdog(struct ifnet *);
int mos_miibus_readreg(struct device *, int, int);
void mos_miibus_writereg(struct device *, int, int, int);
void mos_miibus_statchg(struct device *);
int mos_ifmedia_upd(struct ifnet *);
void mos_ifmedia_sts(struct ifnet *, struct ifmediareq *);
void mos_reset(struct mos_softc *sc);

int mos_reg_read_1(struct mos_softc *, int);
int mos_reg_read_2(struct mos_softc *, int);
int mos_reg_write_1(struct mos_softc *, int, int);
int mos_reg_write_2(struct mos_softc *, int, int);
int mos_readmac(struct mos_softc *, u_char *);
int mos_writemac(struct mos_softc *, u_char *);
int mos_write_mcast(struct mos_softc *, u_char *);

void mos_iff(struct mos_softc *);
void mos_lock_mii(struct mos_softc *);
void mos_unlock_mii(struct mos_softc *);

/*
 * Get exclusive access to the MII registers
 */
void
mos_lock_mii(struct mos_softc *sc)
{
	sc->mos_refcnt++;
	rw_enter_write(&sc->mos_mii_lock);
}

void
mos_unlock_mii(struct mos_softc *sc)
{
	rw_exit_write(&sc->mos_mii_lock);
	if (--sc->mos_refcnt < 0)
		usb_detach_wakeup(&sc->mos_dev);
}

int
mos_reg_read_1(struct mos_softc *sc, int reg)
{
	usb_device_request_t	req;
	usbd_status		err;
	uByte			val = 0;

	if (usbd_is_dying(sc->mos_udev))
		return(0);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = MOS_UR_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 1);

	err = usbd_do_request(sc->mos_udev, &req, &val);

	if (err) {
		DPRINTF(("mos_reg_read_1 error, reg: %d\n", reg));
		return (-1);
	}

	return (val);
}

int
mos_reg_read_2(struct mos_softc *sc, int reg)
{
	usb_device_request_t	req;
	usbd_status		err;
	uWord			val;

	USETW(val,0);

	if (usbd_is_dying(sc->mos_udev))
		return(0);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = MOS_UR_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 2);

	err = usbd_do_request(sc->mos_udev, &req, &val);

	if (err) {
		DPRINTF(("mos_reg_read_2 error, reg: %d\n", reg));
		return (-1);
	}

	return(UGETW(val));
}

int
mos_reg_write_1(struct mos_softc *sc, int reg, int aval)
{
	usb_device_request_t	req;
	usbd_status		err;
	uByte			val;

	val = aval;

	if (usbd_is_dying(sc->mos_udev))
		return(0);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MOS_UR_WRITEREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 1);

	err = usbd_do_request(sc->mos_udev, &req, &val);

	if (err) {
		DPRINTF(("mos_reg_write_1 error, reg: %d\n", reg));
		return (-1);
	}

	return(0);
}

int
mos_reg_write_2(struct mos_softc *sc, int reg, int aval)
{
	usb_device_request_t	req;
	usbd_status		err;
	uWord			val;

	USETW(val, aval);

	if (usbd_is_dying(sc->mos_udev))
		return (0);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MOS_UR_WRITEREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 2);

	err = usbd_do_request(sc->mos_udev, &req, &val);

	if (err) {
		DPRINTF(("mos_reg_write_2 error, reg: %d\n", reg));
		return (-1);
	}

	return (0);
}

int
mos_readmac(struct mos_softc *sc, u_char *mac)
{
	usb_device_request_t	req;
	usbd_status		err;

	if (usbd_is_dying(sc->mos_udev))
		return(0);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = MOS_UR_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, MOS_MAC);
	USETW(req.wLength, ETHER_ADDR_LEN);

	err = usbd_do_request(sc->mos_udev, &req, mac);

	if (err) {
		DPRINTF(("mos_readmac error"));
		return (-1);
	}

	return (0);
}

int
mos_writemac(struct mos_softc *sc, u_char *mac)
{
	usb_device_request_t	req;
	usbd_status		err;

	if (usbd_is_dying(sc->mos_udev))
		return(0);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MOS_UR_WRITEREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, MOS_MAC);
	USETW(req.wLength, ETHER_ADDR_LEN);

	err = usbd_do_request(sc->mos_udev, &req, mac);

	if (err) {
		DPRINTF(("mos_writemac error"));
		return (-1);
	}

	return (0);
}

int
mos_write_mcast(struct mos_softc *sc, u_char *hashtbl)
{
	usb_device_request_t	req;
	usbd_status		err;

	if (usbd_is_dying(sc->mos_udev))
		return(0);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MOS_UR_WRITEREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, MOS_MCAST_TABLE);
	USETW(req.wLength, 8);

	err = usbd_do_request(sc->mos_udev, &req, hashtbl);

	if (err) {
		DPRINTF(("mos_reg_mcast error\n"));
		return(-1);
	}

	return(0);
}

int
mos_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct mos_softc	*sc = (void *)dev;
	int			i,res;

	if (usbd_is_dying(sc->mos_udev)) {
		DPRINTF(("mos: dying\n"));
		return (0);
	}

	mos_lock_mii(sc);

	mos_reg_write_2(sc, MOS_PHY_DATA, 0);
	mos_reg_write_1(sc, MOS_PHY_CTL, (phy & MOS_PHYCTL_PHYADDR) |
	    MOS_PHYCTL_READ);
	mos_reg_write_1(sc, MOS_PHY_STS, (reg & MOS_PHYSTS_PHYREG) |
	    MOS_PHYSTS_PENDING);

	for (i = 0; i < MOS_TIMEOUT; i++) {
		if (mos_reg_read_1(sc, MOS_PHY_STS) & MOS_PHYSTS_READY)
			break;
	}
	if (i == MOS_TIMEOUT) {
		printf("%s: MII read timeout\n", sc->mos_dev.dv_xname);
	}

	res = mos_reg_read_2(sc, MOS_PHY_DATA);

	mos_unlock_mii(sc);

	return (res);
}

void
mos_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct mos_softc	*sc = (void *)dev;
	int			i;

	if (usbd_is_dying(sc->mos_udev))
		return;

	mos_lock_mii(sc);

	mos_reg_write_2(sc, MOS_PHY_DATA, val);
	mos_reg_write_1(sc, MOS_PHY_CTL, (phy & MOS_PHYCTL_PHYADDR) |
	    MOS_PHYCTL_WRITE);
	mos_reg_write_1(sc, MOS_PHY_STS, (reg & MOS_PHYSTS_PHYREG) |
	    MOS_PHYSTS_PENDING);

	for (i = 0; i < MOS_TIMEOUT; i++) {
		if (mos_reg_read_1(sc, MOS_PHY_STS) & MOS_PHYSTS_READY)
			break;
	}
	if (i == MOS_TIMEOUT) {
		printf("%s: MII write timeout\n", sc->mos_dev.dv_xname);
	}

	mos_unlock_mii(sc);

	return;
}

void
mos_miibus_statchg(struct device *dev)
{
	struct mos_softc	*sc = (void *)dev;
	struct mii_data		*mii = GET_MII(sc);
	int			val, err;

	mos_lock_mii(sc);

	/* disable RX, TX prior to changing FDX, SPEEDSEL */
	val = mos_reg_read_1(sc, MOS_CTL);
	val &= ~(MOS_CTL_TX_ENB | MOS_CTL_RX_ENB);
	mos_reg_write_1(sc, MOS_CTL, val);

	/* reset register which counts dropped frames */
	mos_reg_write_1(sc, MOS_FRAME_DROP_CNT, 0);

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX)
		val |= MOS_CTL_FDX_ENB;
	else
		val &= ~(MOS_CTL_FDX_ENB);

	switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_100_TX:
			val |=  MOS_CTL_SPEEDSEL;
			break;
		case IFM_10_T:
			val &= ~(MOS_CTL_SPEEDSEL);
			break;
	}

	/* re-enable TX, RX */
	val |= (MOS_CTL_TX_ENB | MOS_CTL_RX_ENB);
	err = mos_reg_write_1(sc, MOS_CTL, val);
	mos_unlock_mii(sc);

	if (err) {
		printf("%s: media change failed\n", sc->mos_dev.dv_xname);
		return;
	}
}

/*
 * Set media options.
 */
int
mos_ifmedia_upd(struct ifnet *ifp)
{
	struct mos_softc	*sc = ifp->if_softc;
	struct mii_data		*mii = GET_MII(sc);

	sc->mos_link = 0;
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
mos_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct mos_softc	*sc = ifp->if_softc;
	struct mii_data		*mii = GET_MII(sc);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

void
mos_iff(struct mos_softc *sc)
{
	struct ifnet		*ifp = GET_IFP(sc);
	struct arpcom		*ac = &sc->arpcom;
	struct ether_multi	*enm;
	struct ether_multistep	step;
	u_int32_t		h = 0;
	u_int8_t		rxmode, hashtbl[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	if (usbd_is_dying(sc->mos_udev))
		return;

	rxmode = mos_reg_read_1(sc, MOS_CTL);
	rxmode &= ~(MOS_CTL_ALLMULTI | MOS_CTL_RX_PROMISC);
	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		rxmode |= MOS_CTL_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxmode |= MOS_CTL_RX_PROMISC;
	} else {
		/* now program new ones */
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = ether_crc32_be(enm->enm_addrlo, ETHER_ADDR_LEN) >> 26;

			hashtbl[h / 8] |= 1 << (h % 8);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	/* 
	 * The datasheet claims broadcast frames were always accepted
	 * regardless of filter settings. But the hardware seems to
	 * filter broadcast frames, so pass them explicitly.
	 */
	h = ether_crc32_be(etherbroadcastaddr, ETHER_ADDR_LEN) >> 26;
	hashtbl[h / 8] |= 1 << (h % 8);

	mos_write_mcast(sc, (void *)&hashtbl);
	mos_reg_write_1(sc, MOS_CTL, rxmode);
}

void
mos_reset(struct mos_softc *sc)
{
	u_int8_t ctl;
	if (usbd_is_dying(sc->mos_udev))
		return;

	ctl = mos_reg_read_1(sc, MOS_CTL);
	ctl &= ~(MOS_CTL_RX_PROMISC | MOS_CTL_ALLMULTI | MOS_CTL_TX_ENB |
	    MOS_CTL_RX_ENB);
	/* Disable RX, TX, promiscuous and allmulticast mode */
	mos_reg_write_1(sc, MOS_CTL, ctl);

	/* Reset frame drop counter register to zero */
	mos_reg_write_1(sc, MOS_FRAME_DROP_CNT, 0);

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);
	return;
}

void
mos_chip_init(struct mos_softc *sc)
{
	int	i;

	/*
	 * Rev.C devices have a pause threshold register which needs to be set
	 * at startup.
	 */
	if (mos_reg_read_1(sc, MOS_PAUSE_TRHD) != -1) {
		for (i=0;i<MOS_PAUSE_REWRITES;i++)
			mos_reg_write_1(sc, MOS_PAUSE_TRHD, 0);
	}

	sc->mos_phyaddrs[0] = 1; sc->mos_phyaddrs[1] = 0xFF;
}

/*
 * Probe for a MCS7x30 chip.
 */
int
mos_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->configno != MOS_CONFIG_NO)
		return(UMATCH_NONE);

	return (mos_lookup(uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
mos_attach(struct device *parent, struct device *self, void *aux)
{
	struct mos_softc	*sc = (struct mos_softc *)self;
	struct usb_attach_arg	*uaa = aux;
	struct ifnet		*ifp;
	struct usbd_device	*dev = uaa->device;
	usbd_status		err;
	usb_interface_descriptor_t 	*id;
	usb_endpoint_descriptor_t 	*ed;
	struct mii_data 	*mii;
	u_char			eaddr[ETHER_ADDR_LEN];
	int			i,s;

	sc->mos_udev = dev;
	sc->mos_unit = self->dv_unit;

	usb_init_task(&sc->mos_tick_task, mos_tick_task, sc,
	    USB_TASK_TYPE_GENERIC);
	rw_init(&sc->mos_mii_lock, "mosmii");
	usb_init_task(&sc->mos_stop_task, (void (*)(void *))mos_stop, sc,
	    USB_TASK_TYPE_GENERIC);

	err = usbd_device2interface_handle(dev, MOS_IFACE_IDX, &sc->mos_iface);
	if (err) {
		printf("%s: getting interface handle failed\n",
		    sc->mos_dev.dv_xname);
		return;
	}

	sc->mos_flags = mos_lookup(uaa->vendor, uaa->product)->mos_flags;

	id = usbd_get_interface_descriptor(sc->mos_iface);

	sc->mos_bufsz = MOS_BUFSZ;

	/* Find endpoints. */
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->mos_iface, i);
		if (!ed) {
			printf("%s: couldn't get ep %d\n",
			    sc->mos_dev.dv_xname, i);
			return;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->mos_ed[MOS_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->mos_ed[MOS_ENDPT_TX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->mos_ed[MOS_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	s = splnet();

	printf("%s:", sc->mos_dev.dv_xname);

	if (sc->mos_flags & MCS7730)
		printf(" MCS7730");
	else if (sc->mos_flags & MCS7830)
		printf(" MCS7830");
	else if (sc->mos_flags & MCS7832)
		printf(" MCS7832");

	mos_chip_init(sc);

	/*
	 * Read MAC address, inform the world.
	 */
	err = mos_readmac(sc, (void*)&eaddr);
	if (err) {
		printf("%s: couldn't get MAC address\n",
		    sc->mos_dev.dv_xname);
		splx(s);
		return;
	}
	bcopy(eaddr, (char *)&sc->arpcom.ac_enaddr, ETHER_ADDR_LEN);
	printf(", address %s\n", ether_sprintf(eaddr));

	/* Initialize interface info.*/
	ifp = GET_IFP(sc);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = mos_ioctl;
	ifp->if_start = mos_start;
	ifp->if_watchdog = mos_watchdog;
	strlcpy(ifp->if_xname, sc->mos_dev.dv_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/* Initialize MII/media info. */
	mii = GET_MII(sc);
	mii->mii_ifp = ifp;
	mii->mii_readreg = mos_miibus_readreg;
	mii->mii_writereg = mos_miibus_writereg;
	mii->mii_statchg = mos_miibus_statchg;
	mii->mii_flags = MIIF_AUTOTSLEEP;

	ifmedia_init(&mii->mii_media, 0, mos_ifmedia_upd, mos_ifmedia_sts);
	mii_attach(self, mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY, 0);

	if (LIST_FIRST(&mii->mii_phys) == NULL) {
		ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_NONE, 0, NULL);
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_NONE);
	} else
		ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_AUTO);

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->mos_stat_ch, mos_tick, sc);

	splx(s);
}

int
mos_detach(struct device *self, int flags)
{
	struct mos_softc	*sc = (struct mos_softc *)self;
	struct ifnet		*ifp = GET_IFP(sc);
	int			s;

	DPRINTFN(2,("%s: %s: enter\n", sc->mos_dev.dv_xname, __func__));

	if (timeout_initialized(&sc->mos_stat_ch))
		timeout_del(&sc->mos_stat_ch);

	if (sc->mos_ep[MOS_ENDPT_TX] != NULL)
		usbd_abort_pipe(sc->mos_ep[MOS_ENDPT_TX]);
	if (sc->mos_ep[MOS_ENDPT_RX] != NULL)
		usbd_abort_pipe(sc->mos_ep[MOS_ENDPT_RX]);
	if (sc->mos_ep[MOS_ENDPT_INTR] != NULL)
		usbd_abort_pipe(sc->mos_ep[MOS_ENDPT_INTR]);

	/*
	 * Remove any pending tasks.  They cannot be executing because they run
	 * in the same thread as detach.
	 */
	usb_rem_task(sc->mos_udev, &sc->mos_tick_task);
	usb_rem_task(sc->mos_udev, &sc->mos_stop_task);
	s = splusb();

	if (--sc->mos_refcnt >= 0) {
		/* Wait for processes to go away */
		usb_detach_wait(&sc->mos_dev);
	}

	if (ifp->if_flags & IFF_RUNNING)
		mos_stop(sc);

	mii_detach(&sc->mos_mii, MII_PHY_ANY, MII_OFFSET_ANY);
	ifmedia_delete_instance(&sc->mos_mii.mii_media, IFM_INST_ANY);
	if (ifp->if_softc != NULL) {
		ether_ifdetach(ifp);
		if_detach(ifp);
	}

#ifdef DIAGNOSTIC
	if (sc->mos_ep[MOS_ENDPT_TX] != NULL ||
	    sc->mos_ep[MOS_ENDPT_RX] != NULL ||
	    sc->mos_ep[MOS_ENDPT_INTR] != NULL)
		printf("%s: detach has active endpoints\n",
		    sc->mos_dev.dv_xname);
#endif

	splx(s);

	return (0);
}

struct mbuf *
mos_newbuf(void)
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
mos_rx_list_init(struct mos_softc *sc)
{
	struct mos_cdata	*cd;
	struct mos_chain	*c;
	int 			i;

	DPRINTF(("%s: %s: enter\n", sc->mos_dev.dv_xname, __func__));

	cd = &sc->mos_cdata;
	for (i = 0; i < MOS_RX_LIST_CNT; i++) {
		c = &cd->mos_rx_chain[i];
		c->mos_sc = sc;
		c->mos_idx = i;
		c->mos_mbuf = NULL;
		if (c->mos_xfer == NULL) {
			c->mos_xfer = usbd_alloc_xfer(sc->mos_udev);
			if (c->mos_xfer == NULL)
				return (ENOBUFS);
			c->mos_buf = usbd_alloc_buffer(c->mos_xfer,
			    sc->mos_bufsz);
			if (c->mos_buf == NULL) {
				usbd_free_xfer(c->mos_xfer);
				return (ENOBUFS);
			}
		}
	}

	return (0);
}

int
mos_tx_list_init(struct mos_softc *sc)
{
	struct mos_cdata	*cd;
	struct mos_chain	*c;
	int			i;

	DPRINTF(("%s: %s: enter\n", sc->mos_dev.dv_xname, __func__));

	cd = &sc->mos_cdata;
	for (i = 0; i < MOS_TX_LIST_CNT; i++) {
		c = &cd->mos_tx_chain[i];
		c->mos_sc = sc;
		c->mos_idx = i;
		c->mos_mbuf = NULL;
		if (c->mos_xfer == NULL) {
			c->mos_xfer = usbd_alloc_xfer(sc->mos_udev);
			if (c->mos_xfer == NULL)
				return (ENOBUFS);
			c->mos_buf = usbd_alloc_buffer(c->mos_xfer,
			    sc->mos_bufsz);
			if (c->mos_buf == NULL) {
				usbd_free_xfer(c->mos_xfer);
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
mos_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct mos_chain	*c = (struct mos_chain *)priv;
	struct mos_softc	*sc = c->mos_sc;
	struct ifnet		*ifp = GET_IFP(sc);
	u_char			*buf = c->mos_buf;
	u_int8_t		rxstat;
	u_int32_t		total_len;
	u_int16_t		pktlen = 0;
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
	struct mbuf		*m;
	int			s;

	DPRINTFN(10,("%s: %s: enter\n", sc->mos_dev.dv_xname,__func__));

	if (usbd_is_dying(sc->mos_udev))
		return;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		if (usbd_ratecheck(&sc->mos_rx_notice)) {
			printf("%s: usb errors on rx: %s\n",
			    sc->mos_dev.dv_xname, usbd_errstr(status));
		}
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->mos_ep[MOS_ENDPT_RX]);
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	if (total_len <= 1)
		goto done;

	/* evaluate status byte at the end */
	pktlen = total_len - 1;
	rxstat = buf[pktlen] & MOS_RXSTS_MASK;

	if (rxstat != MOS_RXSTS_VALID) {
		DPRINTF(("%s: erroneous frame received: ", 
		    sc->mos_dev.dv_xname));
		if (rxstat & MOS_RXSTS_SHORT_FRAME)
			DPRINTF(("frame size less than 64 bytes\n"));
		if (rxstat & MOS_RXSTS_LARGE_FRAME)
			DPRINTF(("frame size larger than 1532 bytes\n"));
		if (rxstat & MOS_RXSTS_CRC_ERROR)
			DPRINTF(("CRC error\n"));
		if (rxstat & MOS_RXSTS_ALIGN_ERROR)
			DPRINTF(("alignment error\n"));
		ifp->if_ierrors++;
		goto done;
	}

	if ( pktlen < sizeof(struct ether_header) ) {
		ifp->if_ierrors++;
		goto done;
	}

	m = mos_newbuf();
	if (m == NULL) {
		ifp->if_ierrors++;
		goto done;
	}

	m->m_pkthdr.len = m->m_len = pktlen;

	memcpy(mtod(m, char *), buf, pktlen);

	ml_enqueue(&ml, m);

	s = splnet();
	if_input(ifp, &ml);
	splx(s);

done:
	memset(c->mos_buf, 0, sc->mos_bufsz);

	/* Setup new transfer. */
	usbd_setup_xfer(xfer, sc->mos_ep[MOS_ENDPT_RX],
	    c, c->mos_buf, sc->mos_bufsz,
	    USBD_SHORT_XFER_OK | USBD_NO_COPY,
	    USBD_NO_TIMEOUT, mos_rxeof);
	usbd_transfer(xfer);

	DPRINTFN(10,("%s: %s: start rx\n", sc->mos_dev.dv_xname, __func__));

	return;
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

void
mos_txeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct mos_softc	*sc;
	struct mos_chain	*c;
	struct ifnet		*ifp;
	int			s;

	c = priv;
	sc = c->mos_sc;
	ifp = &sc->arpcom.ac_if;

	if (usbd_is_dying(sc->mos_udev))
		return;

	s = splnet();

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			splx(s);
			return;
		}
		ifp->if_oerrors++;
		printf("%s: usb error on tx: %s\n", sc->mos_dev.dv_xname,
		    usbd_errstr(status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(sc->mos_ep[MOS_ENDPT_TX]);
		splx(s);
		return;
	}

	ifp->if_timer = 0;
	ifq_clr_oactive(&ifp->if_snd);

	m_freem(c->mos_mbuf);
	c->mos_mbuf = NULL;

	if (ifq_empty(&ifp->if_snd) == 0)
		mos_start(ifp);

	splx(s);
	return;
}

void
mos_tick(void *xsc)
{
	struct mos_softc *sc = xsc;

	if (sc == NULL)
		return;

	DPRINTFN(0xff, ("%s: %s: enter\n", sc->mos_dev.dv_xname,
			__func__));

	if (usbd_is_dying(sc->mos_udev))
		return;

	/* Perform periodic stuff in process context */
	usb_add_task(sc->mos_udev, &sc->mos_tick_task);

}

void
mos_tick_task(void *xsc)
{
	int			s;
	struct mos_softc	*sc;
	struct ifnet		*ifp;
	struct mii_data		*mii;

	sc = xsc;

	if (sc == NULL)
		return;

	if (usbd_is_dying(sc->mos_udev))
		return;

	ifp = GET_IFP(sc);
	mii = GET_MII(sc);
	if (mii == NULL)
		return;

	s = splnet();

	mii_tick(mii);
	if (!sc->mos_link && mii->mii_media_status & IFM_ACTIVE &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		DPRINTF(("%s: %s: got link\n",
			 sc->mos_dev.dv_xname, __func__));
		sc->mos_link++;
		if (ifq_empty(&ifp->if_snd) == 0)
			mos_start(ifp);
	}

	timeout_add_sec(&sc->mos_stat_ch, 1);

	splx(s);
}

int
mos_encap(struct mos_softc *sc, struct mbuf *m, int idx)
{
	struct mos_chain	*c;
	usbd_status		err;
	int			length;

	c = &sc->mos_cdata.mos_tx_chain[idx];

	m_copydata(m, 0, m->m_pkthdr.len, c->mos_buf);
	length = m->m_pkthdr.len;

	c->mos_mbuf = m;

	usbd_setup_xfer(c->mos_xfer, sc->mos_ep[MOS_ENDPT_TX],
	    c, c->mos_buf, length, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
	    10000, mos_txeof);

	/* Transmit */
	err = usbd_transfer(c->mos_xfer);
	if (err != USBD_IN_PROGRESS) {
		c->mos_mbuf = NULL;
		mos_stop(sc);
		return(EIO);
	}

	sc->mos_cdata.mos_tx_cnt++;

	return(0);
}

void
mos_start(struct ifnet *ifp)
{
	struct mos_softc	*sc;
	struct mbuf		*m_head = NULL;

	sc = ifp->if_softc;

	if (!sc->mos_link)
		return;

	if (ifq_is_oactive(&ifp->if_snd))
		return;

	m_head = ifq_dequeue(&ifp->if_snd);
	if (m_head == NULL)
		return;

	if (mos_encap(sc, m_head, 0)) {
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
mos_init(void *xsc)
{
	struct mos_softc	*sc = xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct mos_chain	*c;
	usbd_status		err;
	u_int8_t		rxmode;
	int			i, s;

	s = splnet();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	mos_reset(sc);

	/*
	 * Write MAC address
	 */
	mos_writemac(sc, sc->arpcom.ac_enaddr);

	/* Init RX ring. */
	if (mos_rx_list_init(sc) == ENOBUFS) {
		printf("%s: rx list init failed\n", sc->mos_dev.dv_xname);
		splx(s);
		return;
	}

	/* Init TX ring. */
	if (mos_tx_list_init(sc) == ENOBUFS) {
		printf("%s: tx list init failed\n", sc->mos_dev.dv_xname);
		splx(s);
		return;
	}

	/* Read and set transmitter IPG values */
	sc->mos_ipgs[0] = mos_reg_read_1(sc, MOS_IPG0);
	sc->mos_ipgs[1] = mos_reg_read_1(sc, MOS_IPG1);
	mos_reg_write_1(sc, MOS_IPG0, sc->mos_ipgs[0]);
	mos_reg_write_1(sc, MOS_IPG1, sc->mos_ipgs[1]);

	/* Program promiscuous mode and multicast filters. */
	mos_iff(sc);

	/* Enable receiver and transmitter, bridge controls speed/duplex mode */
	rxmode = mos_reg_read_1(sc, MOS_CTL);
	rxmode |= MOS_CTL_RX_ENB | MOS_CTL_TX_ENB | MOS_CTL_BS_ENB;
	rxmode &= ~(MOS_CTL_SLEEP);
	mos_reg_write_1(sc, MOS_CTL, rxmode);

	mii_mediachg(GET_MII(sc));

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->mos_iface, sc->mos_ed[MOS_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->mos_ep[MOS_ENDPT_RX]);
	if (err) {
		printf("%s: open rx pipe failed: %s\n",
		    sc->mos_dev.dv_xname, usbd_errstr(err));
		splx(s);
		return;
	}

	err = usbd_open_pipe(sc->mos_iface, sc->mos_ed[MOS_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->mos_ep[MOS_ENDPT_TX]);
	if (err) {
		printf("%s: open tx pipe failed: %s\n",
		    sc->mos_dev.dv_xname, usbd_errstr(err));
		splx(s);
		return;
	}

	/* Start up the receive pipe. */
	for (i = 0; i < MOS_RX_LIST_CNT; i++) {
		c = &sc->mos_cdata.mos_rx_chain[i];
		usbd_setup_xfer(c->mos_xfer, sc->mos_ep[MOS_ENDPT_RX],
		    c, c->mos_buf, sc->mos_bufsz,
		    USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    USBD_NO_TIMEOUT, mos_rxeof);
		usbd_transfer(c->mos_xfer);
	}

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	timeout_add_sec(&sc->mos_stat_ch, 1);
	return;
}

int
mos_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct mos_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *)data;
	int			s, error = 0;

	s = splnet();

	switch(cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			mos_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				mos_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				mos_stop(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->mos_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			mos_iff(sc);
		error = 0;
	}

	splx(s);
	return(error);
}

void
mos_watchdog(struct ifnet *ifp)
{
	struct mos_softc	*sc;
	struct mos_chain	*c;
	usbd_status		stat;
	int			s;

	sc = ifp->if_softc;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->mos_dev.dv_xname);

	s = splusb();
	c = &sc->mos_cdata.mos_tx_chain[0];
	usbd_get_xfer_status(c->mos_xfer, NULL, NULL, NULL, &stat);
	mos_txeof(c->mos_xfer, c, stat);

	if (!ifq_empty(&ifp->if_snd))
		mos_start(ifp);
	splx(s);
}


/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
mos_stop(struct mos_softc *sc)
{
	usbd_status		err;
	struct ifnet		*ifp;
	int			i;

	mos_reset(sc);

	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	timeout_del(&sc->mos_stat_ch);

	/* Stop transfers. */
	if (sc->mos_ep[MOS_ENDPT_RX] != NULL) {
		err = usbd_close_pipe(sc->mos_ep[MOS_ENDPT_RX]);
		if (err) {
			printf("%s: close rx pipe failed: %s\n",
			    sc->mos_dev.dv_xname, usbd_errstr(err));
		}
		sc->mos_ep[MOS_ENDPT_RX] = NULL;
	}

	if (sc->mos_ep[MOS_ENDPT_TX] != NULL) {
		err = usbd_close_pipe(sc->mos_ep[MOS_ENDPT_TX]);
		if (err) {
			printf("%s: close tx pipe failed: %s\n",
			    sc->mos_dev.dv_xname, usbd_errstr(err));
		}
		sc->mos_ep[MOS_ENDPT_TX] = NULL;
	}

	if (sc->mos_ep[MOS_ENDPT_INTR] != NULL) {
		err = usbd_close_pipe(sc->mos_ep[MOS_ENDPT_INTR]);
		if (err) {
			printf("%s: close intr pipe failed: %s\n",
			    sc->mos_dev.dv_xname, usbd_errstr(err));
		}
		sc->mos_ep[MOS_ENDPT_INTR] = NULL;
	}

	/* Free RX resources. */
	for (i = 0; i < MOS_RX_LIST_CNT; i++) {
		if (sc->mos_cdata.mos_rx_chain[i].mos_mbuf != NULL) {
			m_freem(sc->mos_cdata.mos_rx_chain[i].mos_mbuf);
			sc->mos_cdata.mos_rx_chain[i].mos_mbuf = NULL;
		}
		if (sc->mos_cdata.mos_rx_chain[i].mos_xfer != NULL) {
			usbd_free_xfer(sc->mos_cdata.mos_rx_chain[i].mos_xfer);
			sc->mos_cdata.mos_rx_chain[i].mos_xfer = NULL;
		}
	}

	/* Free TX resources. */
	for (i = 0; i < MOS_TX_LIST_CNT; i++) {
		if (sc->mos_cdata.mos_tx_chain[i].mos_mbuf != NULL) {
			m_freem(sc->mos_cdata.mos_tx_chain[i].mos_mbuf);
			sc->mos_cdata.mos_tx_chain[i].mos_mbuf = NULL;
		}
		if (sc->mos_cdata.mos_tx_chain[i].mos_xfer != NULL) {
			usbd_free_xfer(sc->mos_cdata.mos_tx_chain[i].mos_xfer);
			sc->mos_cdata.mos_tx_chain[i].mos_xfer = NULL;
		}
	}

	sc->mos_link = 0;
}

