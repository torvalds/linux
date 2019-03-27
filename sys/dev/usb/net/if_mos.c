/*-
 * SPDX-License-Identifier: (BSD-1-Clause AND BSD-4-Clause)
 *
 * Copyright (c) 2011 Rick van der Zwet <info@rickvanderzwet.nl>
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

/*-
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

/*-
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

/*-
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
 * Moschip MCS7730/MCS7830/MCS7832 USB to Ethernet controller
 * The datasheet is available at the following URL:
 * http://www.moschip.com/data/products/MCS7830/Data%20Sheet_7830.pdf
 */

/*
 * The FreeBSD if_mos.c driver is based on various different sources:
 * The vendor provided driver at the following URL:
 * http://www.moschip.com/data/products/MCS7830/Driver_FreeBSD_7830.tar.gz
 *
 * Mixed together with the OpenBSD if_mos.c driver for validation and checking
 * and the FreeBSD if_reu.c as reference for the USB Ethernet framework.
 */

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>

#include <net/if.h>
#include <net/if_var.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR mos_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/net/usb_ethernet.h>

//#include <dev/usb/net/if_mosreg.h>
#include "if_mosreg.h"

#ifdef USB_DEBUG
static int mos_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, mos, CTLFLAG_RW, 0, "USB mos");
SYSCTL_INT(_hw_usb_mos, OID_AUTO, debug, CTLFLAG_RWTUN, &mos_debug, 0,
    "Debug level");
#endif

#define MOS_DPRINTFN(fmt,...) \
  DPRINTF("mos: %s: " fmt "\n",__FUNCTION__,## __VA_ARGS__)

#define	USB_PRODUCT_MOSCHIP_MCS7730	0x7730
#define	USB_PRODUCT_SITECOMEU_LN030	0x0021



/* Various supported device vendors/products. */
static const STRUCT_USB_HOST_ID mos_devs[] = {
	{USB_VPI(USB_VENDOR_MOSCHIP, USB_PRODUCT_MOSCHIP_MCS7730, MCS7730)},
	{USB_VPI(USB_VENDOR_MOSCHIP, USB_PRODUCT_MOSCHIP_MCS7830, MCS7830)},
	{USB_VPI(USB_VENDOR_MOSCHIP, USB_PRODUCT_MOSCHIP_MCS7832, MCS7832)},
	{USB_VPI(USB_VENDOR_SITECOMEU, USB_PRODUCT_SITECOMEU_LN030, MCS7830)},
};

static int mos_probe(device_t dev);
static int mos_attach(device_t dev);
static void mos_attach_post(struct usb_ether *ue);
static int mos_detach(device_t dev);

static void mos_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error);
static void mos_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error);
static void mos_intr_callback(struct usb_xfer *xfer, usb_error_t error);
static void mos_tick(struct usb_ether *);
static void mos_start(struct usb_ether *);
static void mos_init(struct usb_ether *);
static void mos_chip_init(struct mos_softc *);
static void mos_stop(struct usb_ether *);
static int mos_miibus_readreg(device_t, int, int);
static int mos_miibus_writereg(device_t, int, int, int);
static void mos_miibus_statchg(device_t);
static int mos_ifmedia_upd(struct ifnet *);
static void mos_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static void mos_reset(struct mos_softc *sc);

static int mos_reg_read_1(struct mos_softc *, int);
static int mos_reg_read_2(struct mos_softc *, int);
static int mos_reg_write_1(struct mos_softc *, int, int);
static int mos_reg_write_2(struct mos_softc *, int, int);
static int mos_readmac(struct mos_softc *, uint8_t *);
static int mos_writemac(struct mos_softc *, uint8_t *);
static int mos_write_mcast(struct mos_softc *, u_char *);

static void mos_setmulti(struct usb_ether *);
static void mos_setpromisc(struct usb_ether *);

static const struct usb_config mos_config[MOS_ENDPT_MAX] = {

	[MOS_ENDPT_TX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = (MCLBYTES + 2),
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = mos_bulk_write_callback,
		.timeout = 10000,
	},

	[MOS_ENDPT_RX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = (MCLBYTES + 4 + ETHER_CRC_LEN),
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = mos_bulk_read_callback,
	},

	[MOS_ENDPT_INTR] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,
		.callback = mos_intr_callback,
	},
};

static device_method_t mos_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, mos_probe),
	DEVMETHOD(device_attach, mos_attach),
	DEVMETHOD(device_detach, mos_detach),

	/* MII interface */
	DEVMETHOD(miibus_readreg, mos_miibus_readreg),
	DEVMETHOD(miibus_writereg, mos_miibus_writereg),
	DEVMETHOD(miibus_statchg, mos_miibus_statchg),

	DEVMETHOD_END
};

static driver_t mos_driver = {
	.name = "mos",
	.methods = mos_methods,
	.size = sizeof(struct mos_softc)
};

static devclass_t mos_devclass;

DRIVER_MODULE(mos, uhub, mos_driver, mos_devclass, NULL, 0);
DRIVER_MODULE(miibus, mos, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(mos, uether, 1, 1, 1);
MODULE_DEPEND(mos, usb, 1, 1, 1);
MODULE_DEPEND(mos, ether, 1, 1, 1);
MODULE_DEPEND(mos, miibus, 1, 1, 1);
USB_PNP_HOST_INFO(mos_devs);

static const struct usb_ether_methods mos_ue_methods = {
	.ue_attach_post = mos_attach_post,
	.ue_start = mos_start,
	.ue_init = mos_init,
	.ue_stop = mos_stop,
	.ue_tick = mos_tick,
	.ue_setmulti = mos_setmulti,
	.ue_setpromisc = mos_setpromisc,
	.ue_mii_upd = mos_ifmedia_upd,
	.ue_mii_sts = mos_ifmedia_sts,
};


static int
mos_reg_read_1(struct mos_softc *sc, int reg)
{
	struct usb_device_request req;
	usb_error_t err;
	uByte val = 0;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = MOS_UR_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 1);

	err = uether_do_request(&sc->sc_ue, &req, &val, 1000);

	if (err) {
		MOS_DPRINTFN("mos_reg_read_1 error, reg: %d\n", reg);
		return (-1);
	}
	return (val);
}

static int
mos_reg_read_2(struct mos_softc *sc, int reg)
{
	struct usb_device_request req;
	usb_error_t err;
	uWord val;

	USETW(val, 0);

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = MOS_UR_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 2);

	err = uether_do_request(&sc->sc_ue, &req, &val, 1000);

	if (err) {
		MOS_DPRINTFN("mos_reg_read_2 error, reg: %d", reg);
		return (-1);
	}
	return (UGETW(val));
}

static int
mos_reg_write_1(struct mos_softc *sc, int reg, int aval)
{
	struct usb_device_request req;
	usb_error_t err;
	uByte val;
	val = aval;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MOS_UR_WRITEREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 1);

	err = uether_do_request(&sc->sc_ue, &req, &val, 1000);

	if (err) {
		MOS_DPRINTFN("mos_reg_write_1 error, reg: %d", reg);
		return (-1);
	}
	return (0);
}

static int
mos_reg_write_2(struct mos_softc *sc, int reg, int aval)
{
	struct usb_device_request req;
	usb_error_t err;
	uWord val;

	USETW(val, aval);

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MOS_UR_WRITEREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 2);

	err = uether_do_request(&sc->sc_ue, &req, &val, 1000);

	if (err) {
		MOS_DPRINTFN("mos_reg_write_2 error, reg: %d", reg);
		return (-1);
	}
	return (0);
}

static int
mos_readmac(struct mos_softc *sc, u_char *mac)
{
	struct usb_device_request req;
	usb_error_t err;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = MOS_UR_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, MOS_MAC);
	USETW(req.wLength, ETHER_ADDR_LEN);

	err = uether_do_request(&sc->sc_ue, &req, mac, 1000);

	if (err) {
		return (-1);
	}
	return (0);
}

static int
mos_writemac(struct mos_softc *sc, uint8_t *mac)
{
	struct usb_device_request req;
	usb_error_t err;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MOS_UR_WRITEREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, MOS_MAC);
	USETW(req.wLength, ETHER_ADDR_LEN);

	err = uether_do_request(&sc->sc_ue, &req, mac, 1000);

	if (err) {
		MOS_DPRINTFN("mos_writemac error");
		return (-1);
	}
	return (0);
}

static int
mos_write_mcast(struct mos_softc *sc, u_char *hashtbl)
{
	struct usb_device_request req;
	usb_error_t err;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MOS_UR_WRITEREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, MOS_MCAST_TABLE);
	USETW(req.wLength, 8);

	err = uether_do_request(&sc->sc_ue, &req, hashtbl, 1000);

	if (err) {
		MOS_DPRINTFN("mos_reg_mcast error");
		return (-1);
	}
	return (0);
}

static int
mos_miibus_readreg(device_t dev, int phy, int reg)
{
	struct mos_softc *sc = device_get_softc(dev);
	uWord val;
	int i, res, locked;

	USETW(val, 0);

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		MOS_LOCK(sc);

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
		MOS_DPRINTFN("MII read timeout");
	}
	res = mos_reg_read_2(sc, MOS_PHY_DATA);

	if (!locked)
		MOS_UNLOCK(sc);
	return (res);
}

static int
mos_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct mos_softc *sc = device_get_softc(dev);
	int i, locked;

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		MOS_LOCK(sc);

	mos_reg_write_2(sc, MOS_PHY_DATA, val);
	mos_reg_write_1(sc, MOS_PHY_CTL, (phy & MOS_PHYCTL_PHYADDR) |
	    MOS_PHYCTL_WRITE);
	mos_reg_write_1(sc, MOS_PHY_STS, (reg & MOS_PHYSTS_PHYREG) |
	    MOS_PHYSTS_PENDING);

	for (i = 0; i < MOS_TIMEOUT; i++) {
		if (mos_reg_read_1(sc, MOS_PHY_STS) & MOS_PHYSTS_READY)
			break;
	}
	if (i == MOS_TIMEOUT)
		MOS_DPRINTFN("MII write timeout");

	if (!locked)
		MOS_UNLOCK(sc);
	return 0;
}

static void
mos_miibus_statchg(device_t dev)
{
	struct mos_softc *sc = device_get_softc(dev);
	struct mii_data *mii = GET_MII(sc);
	int val, err, locked;

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		MOS_LOCK(sc);

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
		val |= MOS_CTL_SPEEDSEL;
		break;
	case IFM_10_T:
		val &= ~(MOS_CTL_SPEEDSEL);
		break;
	}

	/* re-enable TX, RX */
	val |= (MOS_CTL_TX_ENB | MOS_CTL_RX_ENB);
	err = mos_reg_write_1(sc, MOS_CTL, val);

	if (err)
		MOS_DPRINTFN("media change failed");

	if (!locked)
		MOS_UNLOCK(sc);
}

/*
 * Set media options.
 */
static int
mos_ifmedia_upd(struct ifnet *ifp)
{
	struct mos_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);
	struct mii_softc *miisc;
	int error;

	MOS_LOCK_ASSERT(sc, MA_OWNED);

	sc->mos_link = 0;
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	return (error);
}

/*
 * Report current media status.
 */
static void
mos_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct mos_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);

	MOS_LOCK(sc);
	mii_pollstat(mii);

	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	MOS_UNLOCK(sc);
}

static void
mos_setpromisc(struct usb_ether *ue)
{
	struct mos_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	uint8_t rxmode;

	MOS_LOCK_ASSERT(sc, MA_OWNED);

	rxmode = mos_reg_read_1(sc, MOS_CTL);

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC) {
		rxmode |= MOS_CTL_RX_PROMISC;
	} else {
		rxmode &= ~MOS_CTL_RX_PROMISC;
	}

	mos_reg_write_1(sc, MOS_CTL, rxmode);
}



static void
mos_setmulti(struct usb_ether *ue)
{
	struct mos_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);
	struct ifmultiaddr *ifma;

	uint32_t h = 0;
	uint8_t rxmode;
	uint8_t hashtbl[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	int allmulti = 0;

	MOS_LOCK_ASSERT(sc, MA_OWNED);

	rxmode = mos_reg_read_1(sc, MOS_CTL);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC)
		allmulti = 1;

	/* get all new ones */
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK) {
			allmulti = 1;
			continue;
		}
		h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
		hashtbl[h / 8] |= 1 << (h % 8);
	}
	if_maddr_runlock(ifp);

	/* now program new ones */
	if (allmulti == 1) {
		rxmode |= MOS_CTL_ALLMULTI;
		mos_reg_write_1(sc, MOS_CTL, rxmode);
	} else {
		rxmode &= ~MOS_CTL_ALLMULTI;
		mos_write_mcast(sc, (void *)&hashtbl);
		mos_reg_write_1(sc, MOS_CTL, rxmode);
	}
}

static void
mos_reset(struct mos_softc *sc)
{
	uint8_t ctl;

	ctl = mos_reg_read_1(sc, MOS_CTL);
	ctl &= ~(MOS_CTL_RX_PROMISC | MOS_CTL_ALLMULTI | MOS_CTL_TX_ENB |
	    MOS_CTL_RX_ENB);
	/* Disable RX, TX, promiscuous and allmulticast mode */
	mos_reg_write_1(sc, MOS_CTL, ctl);

	/* Reset frame drop counter register to zero */
	mos_reg_write_1(sc, MOS_FRAME_DROP_CNT, 0);

	/* Wait a little while for the chip to get its brains in order. */
	usb_pause_mtx(&sc->sc_mtx, hz / 128);
	return;
}

static void
mos_chip_init(struct mos_softc *sc)
{
	int i;

	/*
	 * Rev.C devices have a pause threshold register which needs to be set
	 * at startup.
	 */
	if (mos_reg_read_1(sc, MOS_PAUSE_TRHD) != -1) {
		for (i = 0; i < MOS_PAUSE_REWRITES; i++)
			mos_reg_write_1(sc, MOS_PAUSE_TRHD, 0);
	}
	sc->mos_phyaddrs[0] = 1;
	sc->mos_phyaddrs[1] = 0xFF;
}

/*
 * Probe for a MCS7x30 chip.
 */
static int
mos_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
        int retval;

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != MOS_CONFIG_IDX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != MOS_IFACE_IDX)
		return (ENXIO);

	retval = usbd_lookup_id_by_uaa(mos_devs, sizeof(mos_devs), uaa);
	return (retval);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
mos_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct mos_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;
	uint8_t iface_index;
	int error;

	sc->mos_flags = USB_GET_DRIVER_INFO(uaa);

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	iface_index = MOS_IFACE_IDX;
	error = usbd_transfer_setup(uaa->device, &iface_index,
	    sc->sc_xfer, mos_config, MOS_ENDPT_MAX,
	    sc, &sc->sc_mtx);

	if (error) {
		device_printf(dev, "allocating USB transfers failed\n");
		goto detach;
	}
	ue->ue_sc = sc;
	ue->ue_dev = dev;
	ue->ue_udev = uaa->device;
	ue->ue_mtx = &sc->sc_mtx;
	ue->ue_methods = &mos_ue_methods;


	if (sc->mos_flags & MCS7730) {
		MOS_DPRINTFN("model: MCS7730");
	} else if (sc->mos_flags & MCS7830) {
		MOS_DPRINTFN("model: MCS7830");
	} else if (sc->mos_flags & MCS7832) {
		MOS_DPRINTFN("model: MCS7832");
	}
	error = uether_ifattach(ue);
	if (error) {
		device_printf(dev, "could not attach interface\n");
		goto detach;
	}
	return (0);


detach:
	mos_detach(dev);
	return (ENXIO);
}


static void
mos_attach_post(struct usb_ether *ue)
{
	struct mos_softc *sc = uether_getsc(ue);
        int err;

	/* Read MAC address, inform the world. */
	err = mos_readmac(sc, ue->ue_eaddr);

	if (err)
	  MOS_DPRINTFN("couldn't get MAC address");

	MOS_DPRINTFN("address: %s", ether_sprintf(ue->ue_eaddr));

	mos_chip_init(sc);
}

static int
mos_detach(device_t dev)
{
	struct mos_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;

	usbd_transfer_unsetup(sc->sc_xfer, MOS_ENDPT_MAX);
	uether_ifdetach(ue);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}




/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static void
mos_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct mos_softc *sc = usbd_xfer_softc(xfer);
	struct usb_ether *ue = &sc->sc_ue;
	struct ifnet *ifp = uether_getifp(ue);

	uint8_t rxstat = 0;
	uint32_t actlen;
	uint16_t pktlen = 0;
	struct usb_page_cache *pc;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);
	pc = usbd_xfer_get_frame(xfer, 0);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		MOS_DPRINTFN("actlen : %d", actlen);
		if (actlen <= 1) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto tr_setup;
		}
		/* evaluate status byte at the end */
		usbd_copy_out(pc, actlen - sizeof(rxstat), &rxstat,
		    sizeof(rxstat));

		if (rxstat != MOS_RXSTS_VALID) {
			MOS_DPRINTFN("erroneous frame received");
			if (rxstat & MOS_RXSTS_SHORT_FRAME)
				MOS_DPRINTFN("frame size less than 64 bytes");
			if (rxstat & MOS_RXSTS_LARGE_FRAME) {
				MOS_DPRINTFN("frame size larger than "
				    "1532 bytes");
			}
			if (rxstat & MOS_RXSTS_CRC_ERROR)
				MOS_DPRINTFN("CRC error");
			if (rxstat & MOS_RXSTS_ALIGN_ERROR)
				MOS_DPRINTFN("alignment error");
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto tr_setup;
		}
		/* Remember the last byte was used for the status fields */
		pktlen = actlen - 1;
		if (pktlen < sizeof(struct ether_header)) {
			MOS_DPRINTFN("error: pktlen %d is smaller "
			    "than ether_header %zd", pktlen,
			    sizeof(struct ether_header));
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto tr_setup;
		}
		uether_rxbuf(ue, pc, 0, actlen);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		uether_rxflush(ue);
		return;
	default:
		MOS_DPRINTFN("bulk read error, %s", usbd_errstr(error));
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		MOS_DPRINTFN("start rx %i", usbd_xfer_max_len(xfer));
		return;
	}
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
static void
mos_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct mos_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct usb_page_cache *pc;
	struct mbuf *m;



	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		MOS_DPRINTFN("transfer of complete");
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		/*
		 * XXX: don't send anything if there is no link?
		 */
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			return;

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_m_copy_in(pc, 0, m, 0, m->m_pkthdr.len);

		usbd_xfer_set_frame_len(xfer, 0, m->m_pkthdr.len);


		/*
		 * if there's a BPF listener, bounce a copy
		 * of this frame to him:
		 */
		BPF_MTAP(ifp, m);

		m_freem(m);

		usbd_transfer_submit(xfer);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		return;
	default:
		MOS_DPRINTFN("usb error on tx: %s\n", usbd_errstr(error));
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
mos_tick(struct usb_ether *ue)
{
	struct mos_softc *sc = uether_getsc(ue);
	struct mii_data *mii = GET_MII(sc);

	MOS_LOCK_ASSERT(sc, MA_OWNED);

	mii_tick(mii);
	if (!sc->mos_link && mii->mii_media_status & IFM_ACTIVE &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		MOS_DPRINTFN("got link");
		sc->mos_link++;
		mos_start(ue);
	}
}


static void
mos_start(struct usb_ether *ue)
{
	struct mos_softc *sc = uether_getsc(ue);

	/*
	 * start the USB transfers, if not already started:
	 */
	usbd_transfer_start(sc->sc_xfer[MOS_ENDPT_TX]);
	usbd_transfer_start(sc->sc_xfer[MOS_ENDPT_RX]);
	usbd_transfer_start(sc->sc_xfer[MOS_ENDPT_INTR]);
}

static void
mos_init(struct usb_ether *ue)
{
	struct mos_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);
	uint8_t rxmode;

	MOS_LOCK_ASSERT(sc, MA_OWNED);

	/* Cancel pending I/O and free all RX/TX buffers. */
	mos_reset(sc);

	/* Write MAC address */
	mos_writemac(sc, IF_LLADDR(ifp));

	/* Read and set transmitter IPG values */
	sc->mos_ipgs[0] = mos_reg_read_1(sc, MOS_IPG0);
	sc->mos_ipgs[1] = mos_reg_read_1(sc, MOS_IPG1);
	mos_reg_write_1(sc, MOS_IPG0, sc->mos_ipgs[0]);
	mos_reg_write_1(sc, MOS_IPG1, sc->mos_ipgs[1]);

	/*
	 * Enable receiver and transmitter, bridge controls speed/duplex
	 * mode
	 */
	rxmode = mos_reg_read_1(sc, MOS_CTL);
	rxmode |= MOS_CTL_RX_ENB | MOS_CTL_TX_ENB | MOS_CTL_BS_ENB;
	rxmode &= ~(MOS_CTL_SLEEP);

	mos_setpromisc(ue);

	/* XXX: broadcast mode? */
	mos_reg_write_1(sc, MOS_CTL, rxmode);

	/* Load the multicast filter. */
	mos_setmulti(ue);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	mos_start(ue);
}


static void
mos_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct mos_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct usb_page_cache *pc;
	uint32_t pkt;
	int actlen;

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);
	MOS_DPRINTFN("actlen %i", actlen);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, &pkt, sizeof(pkt));
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		return;
	default:
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}


/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
mos_stop(struct usb_ether *ue)
{
	struct mos_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	mos_reset(sc);

	MOS_LOCK_ASSERT(sc, MA_OWNED);
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;

	/* stop all the transfers, if not already stopped */
	usbd_transfer_stop(sc->sc_xfer[MOS_ENDPT_TX]);
	usbd_transfer_stop(sc->sc_xfer[MOS_ENDPT_RX]);
	usbd_transfer_stop(sc->sc_xfer[MOS_ENDPT_INTR]);

	sc->mos_link = 0;
}
