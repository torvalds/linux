/*	$NetBSD: if_udav.c,v 1.2 2003/09/04 15:17:38 tsutsui Exp $	*/
/*	$nabe: if_udav.c,v 1.3 2003/08/21 16:57:19 nabe Exp $	*/
/*	$FreeBSD$	*/
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003
 *     Shingo WATANABE <nabe@nabechan.org>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *
 */

/*
 * DM9601(DAVICOM USB to Ethernet MAC Controller with Integrated 10/100 PHY)
 * The spec can be found at the following url.
 *   http://ptm2.cc.utu.fi/ftp/network/cards/DM9601/From_NET/DM9601-DS-P01-930914.pdf
 */

/*
 * TODO:
 *	Interrupt Endpoint support
 *	External PHYs
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

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

#define	USB_DEBUG_VAR udav_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/net/usb_ethernet.h>
#include <dev/usb/net/if_udavreg.h>

/* prototypes */

static device_probe_t udav_probe;
static device_attach_t udav_attach;
static device_detach_t udav_detach;

static usb_callback_t udav_bulk_write_callback;
static usb_callback_t udav_bulk_read_callback;
static usb_callback_t udav_intr_callback;

static uether_fn_t udav_attach_post;
static uether_fn_t udav_init;
static uether_fn_t udav_stop;
static uether_fn_t udav_start;
static uether_fn_t udav_tick;
static uether_fn_t udav_setmulti;
static uether_fn_t udav_setpromisc;

static int	udav_csr_read(struct udav_softc *, uint16_t, void *, int);
static int	udav_csr_write(struct udav_softc *, uint16_t, void *, int);
static uint8_t	udav_csr_read1(struct udav_softc *, uint16_t);
static int	udav_csr_write1(struct udav_softc *, uint16_t, uint8_t);
static void	udav_reset(struct udav_softc *);
static int	udav_ifmedia_upd(struct ifnet *);
static void	udav_ifmedia_status(struct ifnet *, struct ifmediareq *);

static miibus_readreg_t udav_miibus_readreg;
static miibus_writereg_t udav_miibus_writereg;
static miibus_statchg_t udav_miibus_statchg;

static const struct usb_config udav_config[UDAV_N_TRANSFER] = {

	[UDAV_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = (MCLBYTES + 2),
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = udav_bulk_write_callback,
		.timeout = 10000,	/* 10 seconds */
	},

	[UDAV_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = (MCLBYTES + 3),
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = udav_bulk_read_callback,
		.timeout = 0,	/* no timeout */
	},

	[UDAV_INTR_DT_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,	/* use wMaxPacketSize */
		.callback = udav_intr_callback,
	},
};

static device_method_t udav_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, udav_probe),
	DEVMETHOD(device_attach, udav_attach),
	DEVMETHOD(device_detach, udav_detach),

	/* MII interface */
	DEVMETHOD(miibus_readreg, udav_miibus_readreg),
	DEVMETHOD(miibus_writereg, udav_miibus_writereg),
	DEVMETHOD(miibus_statchg, udav_miibus_statchg),

	DEVMETHOD_END
};

static driver_t udav_driver = {
	.name = "udav",
	.methods = udav_methods,
	.size = sizeof(struct udav_softc),
};

static devclass_t udav_devclass;

static const STRUCT_USB_HOST_ID udav_devs[] = {
	/* ShanTou DM9601 USB NIC */
	{USB_VPI(USB_VENDOR_SHANTOU, USB_PRODUCT_SHANTOU_DM9601, 0)},
	/* ShanTou ST268 USB NIC */
	{USB_VPI(USB_VENDOR_SHANTOU, USB_PRODUCT_SHANTOU_ST268, 0)},
	/* Corega USB-TXC */
	{USB_VPI(USB_VENDOR_COREGA, USB_PRODUCT_COREGA_FETHER_USB_TXC, 0)},
	/* ShanTou AMD8515 USB NIC */
	{USB_VPI(USB_VENDOR_SHANTOU, USB_PRODUCT_SHANTOU_ADM8515, 0)},
	/* Kontron AG USB Ethernet */
	{USB_VPI(USB_VENDOR_KONTRON, USB_PRODUCT_KONTRON_DM9601, 0)},
	{USB_VPI(USB_VENDOR_KONTRON, USB_PRODUCT_KONTRON_JP1082,
	    UDAV_FLAG_NO_PHY)},
};

DRIVER_MODULE(udav, uhub, udav_driver, udav_devclass, NULL, 0);
DRIVER_MODULE(miibus, udav, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(udav, uether, 1, 1, 1);
MODULE_DEPEND(udav, usb, 1, 1, 1);
MODULE_DEPEND(udav, ether, 1, 1, 1);
MODULE_DEPEND(udav, miibus, 1, 1, 1);
MODULE_VERSION(udav, 1);
USB_PNP_HOST_INFO(udav_devs);

static const struct usb_ether_methods udav_ue_methods = {
	.ue_attach_post = udav_attach_post,
	.ue_start = udav_start,
	.ue_init = udav_init,
	.ue_stop = udav_stop,
	.ue_tick = udav_tick,
	.ue_setmulti = udav_setmulti,
	.ue_setpromisc = udav_setpromisc,
	.ue_mii_upd = udav_ifmedia_upd,
	.ue_mii_sts = udav_ifmedia_status,
};

static const struct usb_ether_methods udav_ue_methods_nophy = {
	.ue_attach_post = udav_attach_post,
	.ue_start = udav_start,
	.ue_init = udav_init,
	.ue_stop = udav_stop,
	.ue_setmulti = udav_setmulti,
	.ue_setpromisc = udav_setpromisc,
};

#ifdef USB_DEBUG
static int udav_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, udav, CTLFLAG_RW, 0, "USB udav");
SYSCTL_INT(_hw_usb_udav, OID_AUTO, debug, CTLFLAG_RWTUN, &udav_debug, 0,
    "Debug level");
#endif

#define	UDAV_SETBIT(sc, reg, x)	\
	udav_csr_write1(sc, reg, udav_csr_read1(sc, reg) | (x))

#define	UDAV_CLRBIT(sc, reg, x)	\
	udav_csr_write1(sc, reg, udav_csr_read1(sc, reg) & ~(x))

static void
udav_attach_post(struct usb_ether *ue)
{
	struct udav_softc *sc = uether_getsc(ue);

	/* reset the adapter */
	udav_reset(sc);

	/* Get Ethernet Address */
	udav_csr_read(sc, UDAV_PAR, ue->ue_eaddr, ETHER_ADDR_LEN);
}

static int
udav_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != UDAV_CONFIG_INDEX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != UDAV_IFACE_INDEX)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(udav_devs, sizeof(udav_devs), uaa));
}

static int
udav_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct udav_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;
	uint8_t iface_index;
	int error;

	sc->sc_flags = USB_GET_DRIVER_INFO(uaa);

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	iface_index = UDAV_IFACE_INDEX;
	error = usbd_transfer_setup(uaa->device, &iface_index,
	    sc->sc_xfer, udav_config, UDAV_N_TRANSFER, sc, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "allocating USB transfers failed\n");
		goto detach;
	}

	/*
	 * The JP1082 has an unusable PHY and provides no link information.
	 */
	if (sc->sc_flags & UDAV_FLAG_NO_PHY) {
		ue->ue_methods = &udav_ue_methods_nophy;
		sc->sc_flags |= UDAV_FLAG_LINK;
	} else {
		ue->ue_methods = &udav_ue_methods;
	}

	ue->ue_sc = sc;
	ue->ue_dev = dev;
	ue->ue_udev = uaa->device;
	ue->ue_mtx = &sc->sc_mtx;

	error = uether_ifattach(ue);
	if (error) {
		device_printf(dev, "could not attach interface\n");
		goto detach;
	}

	return (0);			/* success */

detach:
	udav_detach(dev);
	return (ENXIO);			/* failure */
}

static int
udav_detach(device_t dev)
{
	struct udav_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;

	usbd_transfer_unsetup(sc->sc_xfer, UDAV_N_TRANSFER);
	uether_ifdetach(ue);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

#if 0
static int
udav_mem_read(struct udav_softc *sc, uint16_t offset, void *buf,
    int len)
{
	struct usb_device_request req;

	len &= 0xff;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = UDAV_REQ_MEM_READ;
	USETW(req.wValue, 0x0000);
	USETW(req.wIndex, offset);
	USETW(req.wLength, len);

	return (uether_do_request(&sc->sc_ue, &req, buf, 1000));
}

static int
udav_mem_write(struct udav_softc *sc, uint16_t offset, void *buf,
    int len)
{
	struct usb_device_request req;

	len &= 0xff;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UDAV_REQ_MEM_WRITE;
	USETW(req.wValue, 0x0000);
	USETW(req.wIndex, offset);
	USETW(req.wLength, len);

	return (uether_do_request(&sc->sc_ue, &req, buf, 1000));
}

static int
udav_mem_write1(struct udav_softc *sc, uint16_t offset,
    uint8_t ch)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UDAV_REQ_MEM_WRITE1;
	USETW(req.wValue, ch);
	USETW(req.wIndex, offset);
	USETW(req.wLength, 0x0000);

	return (uether_do_request(&sc->sc_ue, &req, NULL, 1000));
}
#endif

static int
udav_csr_read(struct udav_softc *sc, uint16_t offset, void *buf, int len)
{
	struct usb_device_request req;

	len &= 0xff;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = UDAV_REQ_REG_READ;
	USETW(req.wValue, 0x0000);
	USETW(req.wIndex, offset);
	USETW(req.wLength, len);

	return (uether_do_request(&sc->sc_ue, &req, buf, 1000));
}

static int
udav_csr_write(struct udav_softc *sc, uint16_t offset, void *buf, int len)
{
	struct usb_device_request req;

	offset &= 0xff;
	len &= 0xff;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UDAV_REQ_REG_WRITE;
	USETW(req.wValue, 0x0000);
	USETW(req.wIndex, offset);
	USETW(req.wLength, len);

	return (uether_do_request(&sc->sc_ue, &req, buf, 1000));
}

static uint8_t
udav_csr_read1(struct udav_softc *sc, uint16_t offset)
{
	uint8_t val;

	udav_csr_read(sc, offset, &val, 1);
	return (val);
}

static int
udav_csr_write1(struct udav_softc *sc, uint16_t offset,
    uint8_t ch)
{
	struct usb_device_request req;

	offset &= 0xff;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UDAV_REQ_REG_WRITE1;
	USETW(req.wValue, ch);
	USETW(req.wIndex, offset);
	USETW(req.wLength, 0x0000);

	return (uether_do_request(&sc->sc_ue, &req, NULL, 1000));
}

static void
udav_init(struct usb_ether *ue)
{
	struct udav_softc *sc = ue->ue_sc;
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);

	UDAV_LOCK_ASSERT(sc, MA_OWNED);

	/*
	 * Cancel pending I/O
	 */
	udav_stop(ue);

	/* set MAC address */
	udav_csr_write(sc, UDAV_PAR, IF_LLADDR(ifp), ETHER_ADDR_LEN);

	/* initialize network control register */

	/* disable loopback  */
	UDAV_CLRBIT(sc, UDAV_NCR, UDAV_NCR_LBK0 | UDAV_NCR_LBK1);

	/* Initialize RX control register */
	UDAV_SETBIT(sc, UDAV_RCR, UDAV_RCR_DIS_LONG | UDAV_RCR_DIS_CRC);

	/* load multicast filter and update promiscious mode bit */
	udav_setpromisc(ue);

	/* enable RX */
	UDAV_SETBIT(sc, UDAV_RCR, UDAV_RCR_RXEN);

	/* clear POWER_DOWN state of internal PHY */
	UDAV_SETBIT(sc, UDAV_GPCR, UDAV_GPCR_GEP_CNTL0);
	UDAV_CLRBIT(sc, UDAV_GPR, UDAV_GPR_GEPIO0);

	usbd_xfer_set_stall(sc->sc_xfer[UDAV_BULK_DT_WR]);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	udav_start(ue);
}

static void
udav_reset(struct udav_softc *sc)
{
	int i;

	/* Select PHY */
#if 1
	/*
	 * XXX: force select internal phy.
	 *	external phy routines are not tested.
	 */
	UDAV_CLRBIT(sc, UDAV_NCR, UDAV_NCR_EXT_PHY);
#else
	if (sc->sc_flags & UDAV_EXT_PHY)
		UDAV_SETBIT(sc, UDAV_NCR, UDAV_NCR_EXT_PHY);
	else
		UDAV_CLRBIT(sc, UDAV_NCR, UDAV_NCR_EXT_PHY);
#endif

	UDAV_SETBIT(sc, UDAV_NCR, UDAV_NCR_RST);

	for (i = 0; i < UDAV_TX_TIMEOUT; i++) {
		if (!(udav_csr_read1(sc, UDAV_NCR) & UDAV_NCR_RST))
			break;
		if (uether_pause(&sc->sc_ue, hz / 100))
			break;
	}

	uether_pause(&sc->sc_ue, hz / 100);
}

#define	UDAV_BITS	6
static void
udav_setmulti(struct usb_ether *ue)
{
	struct udav_softc *sc = ue->ue_sc;
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct ifmultiaddr *ifma;
	uint8_t hashtbl[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	int h = 0;

	UDAV_LOCK_ASSERT(sc, MA_OWNED);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		UDAV_SETBIT(sc, UDAV_RCR, UDAV_RCR_ALL|UDAV_RCR_PRMSC);
		return;
	}

	/* first, zot all the existing hash bits */
	memset(hashtbl, 0x00, sizeof(hashtbl));
	hashtbl[7] |= 0x80;	/* broadcast address */
	udav_csr_write(sc, UDAV_MAR, hashtbl, sizeof(hashtbl));

	/* now program new ones */
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

	/* disable all multicast */
	UDAV_CLRBIT(sc, UDAV_RCR, UDAV_RCR_ALL);

	/* write hash value to the register */
	udav_csr_write(sc, UDAV_MAR, hashtbl, sizeof(hashtbl));
}

static void
udav_setpromisc(struct usb_ether *ue)
{
	struct udav_softc *sc = ue->ue_sc;
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	uint8_t rxmode;

	rxmode = udav_csr_read1(sc, UDAV_RCR);
	rxmode &= ~(UDAV_RCR_ALL | UDAV_RCR_PRMSC);

	if (ifp->if_flags & IFF_PROMISC)
		rxmode |= UDAV_RCR_ALL | UDAV_RCR_PRMSC;
	else if (ifp->if_flags & IFF_ALLMULTI)
		rxmode |= UDAV_RCR_ALL;

	/* write new mode bits */
	udav_csr_write1(sc, UDAV_RCR, rxmode);
}

static void
udav_start(struct usb_ether *ue)
{
	struct udav_softc *sc = ue->ue_sc;

	/*
	 * start the USB transfers, if not already started:
	 */
	usbd_transfer_start(sc->sc_xfer[UDAV_INTR_DT_RD]);
	usbd_transfer_start(sc->sc_xfer[UDAV_BULK_DT_RD]);
	usbd_transfer_start(sc->sc_xfer[UDAV_BULK_DT_WR]);
}

static void
udav_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct udav_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct usb_page_cache *pc;
	struct mbuf *m;
	int extra_len;
	int temp_len;
	uint8_t buf[2];

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(11, "transfer complete\n");
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		if ((sc->sc_flags & UDAV_FLAG_LINK) == 0) {
			/*
			 * don't send anything if there is no link !
			 */
			return;
		}
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);

		if (m == NULL)
			return;
		if (m->m_pkthdr.len > MCLBYTES)
			m->m_pkthdr.len = MCLBYTES;
		if (m->m_pkthdr.len < UDAV_MIN_FRAME_LEN) {
			extra_len = UDAV_MIN_FRAME_LEN - m->m_pkthdr.len;
		} else {
			extra_len = 0;
		}

		temp_len = (m->m_pkthdr.len + extra_len);

		/*
		 * the frame length is specified in the first 2 bytes of the
		 * buffer
		 */
		buf[0] = (uint8_t)(temp_len);
		buf[1] = (uint8_t)(temp_len >> 8);

		temp_len += 2;

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_in(pc, 0, buf, 2);
		usbd_m_copy_in(pc, 2, m, 0, m->m_pkthdr.len);

		if (extra_len)
			usbd_frame_zero(pc, temp_len - extra_len, extra_len);
		/*
		 * if there's a BPF listener, bounce a copy
		 * of this frame to him:
		 */
		BPF_MTAP(ifp, m);

		m_freem(m);

		usbd_xfer_set_frame_len(xfer, 0, temp_len);
		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		DPRINTFN(11, "transfer error, %s\n",
		    usbd_errstr(error));

		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
udav_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct udav_softc *sc = usbd_xfer_softc(xfer);
	struct usb_ether *ue = &sc->sc_ue;
	struct ifnet *ifp = uether_getifp(ue);
	struct usb_page_cache *pc;
	struct udav_rxpkt stat;
	int len;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		if (actlen < (int)(sizeof(stat) + ETHER_CRC_LEN)) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto tr_setup;
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, &stat, sizeof(stat));
		actlen -= sizeof(stat);
		len = min(actlen, le16toh(stat.pktlen));
		len -= ETHER_CRC_LEN;

		if (stat.rxstat & UDAV_RSR_LCS) {
			if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
			goto tr_setup;
		}
		if (stat.rxstat & UDAV_RSR_ERR) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto tr_setup;
		}
		uether_rxbuf(ue, pc, sizeof(stat), len);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		uether_rxflush(ue);
		return;

	default:			/* Error */
		DPRINTF("bulk read error, %s\n",
		    usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
udav_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
udav_stop(struct usb_ether *ue)
{
	struct udav_softc *sc = ue->ue_sc;
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);

	UDAV_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	if (!(sc->sc_flags & UDAV_FLAG_NO_PHY))
		sc->sc_flags &= ~UDAV_FLAG_LINK;

	/*
	 * stop all the transfers, if not already stopped:
	 */
	usbd_transfer_stop(sc->sc_xfer[UDAV_BULK_DT_WR]);
	usbd_transfer_stop(sc->sc_xfer[UDAV_BULK_DT_RD]);
	usbd_transfer_stop(sc->sc_xfer[UDAV_INTR_DT_RD]);

	udav_reset(sc);
}

static int
udav_ifmedia_upd(struct ifnet *ifp)
{
	struct udav_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);
	struct mii_softc *miisc;
	int error;

	UDAV_LOCK_ASSERT(sc, MA_OWNED);

        sc->sc_flags &= ~UDAV_FLAG_LINK;
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	return (error);
}

static void
udav_ifmedia_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct udav_softc *sc = ifp->if_softc;
	struct mii_data *mii = GET_MII(sc);

	UDAV_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	UDAV_UNLOCK(sc);
}

static void
udav_tick(struct usb_ether *ue)
{
	struct udav_softc *sc = ue->ue_sc;
	struct mii_data *mii = GET_MII(sc);

	UDAV_LOCK_ASSERT(sc, MA_OWNED);

	mii_tick(mii);
	if ((sc->sc_flags & UDAV_FLAG_LINK) == 0
	    && mii->mii_media_status & IFM_ACTIVE &&
	    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
		sc->sc_flags |= UDAV_FLAG_LINK;
		udav_start(ue);
	}
}

static int
udav_miibus_readreg(device_t dev, int phy, int reg)
{
	struct udav_softc *sc = device_get_softc(dev);
	uint16_t data16;
	uint8_t val[2];
	int locked;

	/* XXX: one PHY only for the internal PHY */
	if (phy != 0)
		return (0);

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		UDAV_LOCK(sc);

	/* select internal PHY and set PHY register address */
	udav_csr_write1(sc, UDAV_EPAR,
	    UDAV_EPAR_PHY_ADR0 | (reg & UDAV_EPAR_EROA_MASK));

	/* select PHY operation and start read command */
	udav_csr_write1(sc, UDAV_EPCR, UDAV_EPCR_EPOS | UDAV_EPCR_ERPRR);

	/* XXX: should we wait? */

	/* end read command */
	UDAV_CLRBIT(sc, UDAV_EPCR, UDAV_EPCR_ERPRR);

	/* retrieve the result from data registers */
	udav_csr_read(sc, UDAV_EPDRL, val, 2);

	data16 = (val[0] | (val[1] << 8));

	DPRINTFN(11, "phy=%d reg=0x%04x => 0x%04x\n",
	    phy, reg, data16);

	if (!locked)
		UDAV_UNLOCK(sc);
	return (data16);
}

static int
udav_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct udav_softc *sc = device_get_softc(dev);
	uint8_t val[2];
	int locked;

	/* XXX: one PHY only for the internal PHY */
	if (phy != 0)
		return (0);

	locked = mtx_owned(&sc->sc_mtx);
	if (!locked)
		UDAV_LOCK(sc);

	/* select internal PHY and set PHY register address */
	udav_csr_write1(sc, UDAV_EPAR,
	    UDAV_EPAR_PHY_ADR0 | (reg & UDAV_EPAR_EROA_MASK));

	/* put the value to the data registers */
	val[0] = (data & 0xff);
	val[1] = (data >> 8) & 0xff;
	udav_csr_write(sc, UDAV_EPDRL, val, 2);

	/* select PHY operation and start write command */
	udav_csr_write1(sc, UDAV_EPCR, UDAV_EPCR_EPOS | UDAV_EPCR_ERPRW);

	/* XXX: should we wait? */

	/* end write command */
	UDAV_CLRBIT(sc, UDAV_EPCR, UDAV_EPCR_ERPRW);

	if (!locked)
		UDAV_UNLOCK(sc);
	return (0);
}

static void
udav_miibus_statchg(device_t dev)
{
	/* nothing to do */
}
