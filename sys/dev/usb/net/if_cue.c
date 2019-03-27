/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999, 2000
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
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
 * CATC USB-EL1210A USB to ethernet driver. Used in the CATC Netmate
 * adapters and others.
 *
 * Written by Bill Paul <wpaul@ee.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The CATC USB-EL1210A provides USB ethernet support at 10Mbps. The
 * RX filter uses a 512-bit multicast hash table, single perfect entry
 * for the station address, and promiscuous mode. Unlike the ADMtek
 * and KLSI chips, the CATC ASIC supports read and write combining
 * mode where multiple packets can be transferred using a single bulk
 * transaction, which helps performance a great deal.
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

#define	USB_DEBUG_VAR cue_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/net/usb_ethernet.h>
#include <dev/usb/net/if_cuereg.h>

/*
 * Various supported device vendors/products.
 */

/* Belkin F5U111 adapter covered by NETMATE entry */

static const STRUCT_USB_HOST_ID cue_devs[] = {
#define	CUE_DEV(v,p) { USB_VP(USB_VENDOR_##v, USB_PRODUCT_##v##_##p) }
	CUE_DEV(CATC, NETMATE),
	CUE_DEV(CATC, NETMATE2),
	CUE_DEV(SMARTBRIDGES, SMARTLINK),
#undef CUE_DEV
};

/* prototypes */

static device_probe_t cue_probe;
static device_attach_t cue_attach;
static device_detach_t cue_detach;

static usb_callback_t cue_bulk_read_callback;
static usb_callback_t cue_bulk_write_callback;

static uether_fn_t cue_attach_post;
static uether_fn_t cue_init;
static uether_fn_t cue_stop;
static uether_fn_t cue_start;
static uether_fn_t cue_tick;
static uether_fn_t cue_setmulti;
static uether_fn_t cue_setpromisc;

static uint8_t	cue_csr_read_1(struct cue_softc *, uint16_t);
static uint16_t	cue_csr_read_2(struct cue_softc *, uint8_t);
static int	cue_csr_write_1(struct cue_softc *, uint16_t, uint16_t);
static int	cue_mem(struct cue_softc *, uint8_t, uint16_t, void *, int);
static int	cue_getmac(struct cue_softc *, void *);
static uint32_t	cue_mchash(const uint8_t *);
static void	cue_reset(struct cue_softc *);

#ifdef USB_DEBUG
static int cue_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, cue, CTLFLAG_RW, 0, "USB cue");
SYSCTL_INT(_hw_usb_cue, OID_AUTO, debug, CTLFLAG_RWTUN, &cue_debug, 0,
    "Debug level");
#endif

static const struct usb_config cue_config[CUE_N_TRANSFER] = {

	[CUE_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = (MCLBYTES + 2),
		.flags = {.pipe_bof = 1,},
		.callback = cue_bulk_write_callback,
		.timeout = 10000,	/* 10 seconds */
	},

	[CUE_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = (MCLBYTES + 2),
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = cue_bulk_read_callback,
	},
};

static device_method_t cue_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, cue_probe),
	DEVMETHOD(device_attach, cue_attach),
	DEVMETHOD(device_detach, cue_detach),

	DEVMETHOD_END
};

static driver_t cue_driver = {
	.name = "cue",
	.methods = cue_methods,
	.size = sizeof(struct cue_softc),
};

static devclass_t cue_devclass;

DRIVER_MODULE(cue, uhub, cue_driver, cue_devclass, NULL, 0);
MODULE_DEPEND(cue, uether, 1, 1, 1);
MODULE_DEPEND(cue, usb, 1, 1, 1);
MODULE_DEPEND(cue, ether, 1, 1, 1);
MODULE_VERSION(cue, 1);
USB_PNP_HOST_INFO(cue_devs);

static const struct usb_ether_methods cue_ue_methods = {
	.ue_attach_post = cue_attach_post,
	.ue_start = cue_start,
	.ue_init = cue_init,
	.ue_stop = cue_stop,
	.ue_tick = cue_tick,
	.ue_setmulti = cue_setmulti,
	.ue_setpromisc = cue_setpromisc,
};

#define	CUE_SETBIT(sc, reg, x)				\
	cue_csr_write_1(sc, reg, cue_csr_read_1(sc, reg) | (x))

#define	CUE_CLRBIT(sc, reg, x)				\
	cue_csr_write_1(sc, reg, cue_csr_read_1(sc, reg) & ~(x))

static uint8_t
cue_csr_read_1(struct cue_softc *sc, uint16_t reg)
{
	struct usb_device_request req;
	uint8_t val;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = CUE_CMD_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 1);

	if (uether_do_request(&sc->sc_ue, &req, &val, 1000)) {
		/* ignore any errors */
	}
	return (val);
}

static uint16_t
cue_csr_read_2(struct cue_softc *sc, uint8_t reg)
{
	struct usb_device_request req;
	uint16_t val;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = CUE_CMD_READREG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 2);

	(void)uether_do_request(&sc->sc_ue, &req, &val, 1000);
	return (le16toh(val));
}

static int
cue_csr_write_1(struct cue_softc *sc, uint16_t reg, uint16_t val)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = CUE_CMD_WRITEREG;
	USETW(req.wValue, val);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 0);

	return (uether_do_request(&sc->sc_ue, &req, NULL, 1000));
}

static int
cue_mem(struct cue_softc *sc, uint8_t cmd, uint16_t addr, void *buf, int len)
{
	struct usb_device_request req;

	if (cmd == CUE_CMD_READSRAM)
		req.bmRequestType = UT_READ_VENDOR_DEVICE;
	else
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = cmd;
	USETW(req.wValue, 0);
	USETW(req.wIndex, addr);
	USETW(req.wLength, len);

	return (uether_do_request(&sc->sc_ue, &req, buf, 1000));
}

static int
cue_getmac(struct cue_softc *sc, void *buf)
{
	struct usb_device_request req;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = CUE_CMD_GET_MACADDR;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, ETHER_ADDR_LEN);

	return (uether_do_request(&sc->sc_ue, &req, buf, 1000));
}

#define	CUE_BITS 9

static uint32_t
cue_mchash(const uint8_t *addr)
{
	uint32_t crc;

	/* Compute CRC for the address value. */
	crc = ether_crc32_le(addr, ETHER_ADDR_LEN);

	return (crc & ((1 << CUE_BITS) - 1));
}

static void
cue_setpromisc(struct usb_ether *ue)
{
	struct cue_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	CUE_LOCK_ASSERT(sc, MA_OWNED);

	/* if we want promiscuous mode, set the allframes bit */
	if (ifp->if_flags & IFF_PROMISC)
		CUE_SETBIT(sc, CUE_ETHCTL, CUE_ETHCTL_PROMISC);
	else
		CUE_CLRBIT(sc, CUE_ETHCTL, CUE_ETHCTL_PROMISC);

	/* write multicast hash-bits */
	cue_setmulti(ue);
}

static void
cue_setmulti(struct usb_ether *ue)
{
	struct cue_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);
	struct ifmultiaddr *ifma;
	uint32_t h = 0, i;
	uint8_t hashtbl[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	CUE_LOCK_ASSERT(sc, MA_OWNED);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		for (i = 0; i < 8; i++)
			hashtbl[i] = 0xff;
		cue_mem(sc, CUE_CMD_WRITESRAM, CUE_MCAST_TABLE_ADDR,
		    &hashtbl, 8);
		return;
	}

	/* now program new ones */
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link)
	{
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = cue_mchash(LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
		hashtbl[h >> 3] |= 1 << (h & 0x7);
	}
	if_maddr_runlock(ifp);

	/*
	 * Also include the broadcast address in the filter
	 * so we can receive broadcast frames.
 	 */
	if (ifp->if_flags & IFF_BROADCAST) {
		h = cue_mchash(ifp->if_broadcastaddr);
		hashtbl[h >> 3] |= 1 << (h & 0x7);
	}

	cue_mem(sc, CUE_CMD_WRITESRAM, CUE_MCAST_TABLE_ADDR, &hashtbl, 8);
}

static void
cue_reset(struct cue_softc *sc)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = CUE_CMD_RESET;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	if (uether_do_request(&sc->sc_ue, &req, NULL, 1000)) {
		/* ignore any errors */
	}

	/*
	 * wait a little while for the chip to get its brains in order:
	 */
	uether_pause(&sc->sc_ue, hz / 100);
}

static void
cue_attach_post(struct usb_ether *ue)
{
	struct cue_softc *sc = uether_getsc(ue);

	cue_getmac(sc, ue->ue_eaddr);
}

static int
cue_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != CUE_CONFIG_IDX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != CUE_IFACE_IDX)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(cue_devs, sizeof(cue_devs), uaa));
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
cue_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct cue_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;
	uint8_t iface_index;
	int error;

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	iface_index = CUE_IFACE_IDX;
	error = usbd_transfer_setup(uaa->device, &iface_index,
	    sc->sc_xfer, cue_config, CUE_N_TRANSFER, sc, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "allocating USB transfers failed\n");
		goto detach;
	}

	ue->ue_sc = sc;
	ue->ue_dev = dev;
	ue->ue_udev = uaa->device;
	ue->ue_mtx = &sc->sc_mtx;
	ue->ue_methods = &cue_ue_methods;

	error = uether_ifattach(ue);
	if (error) {
		device_printf(dev, "could not attach interface\n");
		goto detach;
	}
	return (0);			/* success */

detach:
	cue_detach(dev);
	return (ENXIO);			/* failure */
}

static int
cue_detach(device_t dev)
{
	struct cue_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;

	usbd_transfer_unsetup(sc->sc_xfer, CUE_N_TRANSFER);
	uether_ifdetach(ue);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static void
cue_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct cue_softc *sc = usbd_xfer_softc(xfer);
	struct usb_ether *ue = &sc->sc_ue;
	struct ifnet *ifp = uether_getifp(ue);
	struct usb_page_cache *pc;
	uint8_t buf[2];
	int len;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		if (actlen <= (int)(2 + sizeof(struct ether_header))) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto tr_setup;
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, buf, 2);
		actlen -= 2;
		len = buf[0] | (buf[1] << 8);
		len = min(actlen, len);

		uether_rxbuf(ue, pc, 2, len);
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
cue_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct cue_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct usb_page_cache *pc;
	struct mbuf *m;
	uint8_t buf[2];

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(11, "transfer complete\n");
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);

		if (m == NULL)
			return;
		if (m->m_pkthdr.len > MCLBYTES)
			m->m_pkthdr.len = MCLBYTES;
		usbd_xfer_set_frame_len(xfer, 0, (m->m_pkthdr.len + 2));

		/* the first two bytes are the frame length */

		buf[0] = (uint8_t)(m->m_pkthdr.len);
		buf[1] = (uint8_t)(m->m_pkthdr.len >> 8);

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_in(pc, 0, buf, 2);
		usbd_m_copy_in(pc, 2, m, 0, m->m_pkthdr.len);

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		BPF_MTAP(ifp, m);

		m_freem(m);

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
cue_tick(struct usb_ether *ue)
{
	struct cue_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	CUE_LOCK_ASSERT(sc, MA_OWNED);

	if_inc_counter(ifp, IFCOUNTER_COLLISIONS, cue_csr_read_2(sc, CUE_TX_SINGLECOLL));
	if_inc_counter(ifp, IFCOUNTER_COLLISIONS, cue_csr_read_2(sc, CUE_TX_MULTICOLL));
	if_inc_counter(ifp, IFCOUNTER_COLLISIONS, cue_csr_read_2(sc, CUE_TX_EXCESSCOLL));

	if (cue_csr_read_2(sc, CUE_RX_FRAMEERR))
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
}

static void
cue_start(struct usb_ether *ue)
{
	struct cue_softc *sc = uether_getsc(ue);

	/*
	 * start the USB transfers, if not already started:
	 */
	usbd_transfer_start(sc->sc_xfer[CUE_BULK_DT_RD]);
	usbd_transfer_start(sc->sc_xfer[CUE_BULK_DT_WR]);
}

static void
cue_init(struct usb_ether *ue)
{
	struct cue_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);
	int i;

	CUE_LOCK_ASSERT(sc, MA_OWNED);

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	cue_stop(ue);
#if 0
	cue_reset(sc);
#endif
	/* Set MAC address */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		cue_csr_write_1(sc, CUE_PAR0 - i, IF_LLADDR(ifp)[i]);

	/* Enable RX logic. */
	cue_csr_write_1(sc, CUE_ETHCTL, CUE_ETHCTL_RX_ON | CUE_ETHCTL_MCAST_ON);

	/* Load the multicast filter */
	cue_setpromisc(ue);

	/*
	 * Set the number of RX and TX buffers that we want
	 * to reserve inside the ASIC.
	 */
	cue_csr_write_1(sc, CUE_RX_BUFPKTS, CUE_RX_FRAMES);
	cue_csr_write_1(sc, CUE_TX_BUFPKTS, CUE_TX_FRAMES);

	/* Set advanced operation modes. */
	cue_csr_write_1(sc, CUE_ADVANCED_OPMODES,
	    CUE_AOP_EMBED_RXLEN | 0x01);/* 1 wait state */

	/* Program the LED operation. */
	cue_csr_write_1(sc, CUE_LEDCTL, CUE_LEDCTL_FOLLOW_LINK);

	usbd_xfer_set_stall(sc->sc_xfer[CUE_BULK_DT_WR]);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	cue_start(ue);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
cue_stop(struct usb_ether *ue)
{
	struct cue_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	CUE_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;

	/*
	 * stop all the transfers, if not already stopped:
	 */
	usbd_transfer_stop(sc->sc_xfer[CUE_BULK_DT_WR]);
	usbd_transfer_stop(sc->sc_xfer[CUE_BULK_DT_RD]);

	cue_csr_write_1(sc, CUE_ETHCTL, 0);
	cue_reset(sc);
}
