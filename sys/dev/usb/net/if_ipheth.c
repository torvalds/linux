/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Hans Petter Selasky. All rights reserved.
 * Copyright (c) 2009 Diego Giagio. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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

/*
 * Thanks to Diego Giagio for figuring out the programming details for
 * the Apple iPhone Ethernet driver.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/socket.h>
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

#define	USB_DEBUG_VAR ipheth_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/net/usb_ethernet.h>
#include <dev/usb/net/if_iphethvar.h>

static device_probe_t ipheth_probe;
static device_attach_t ipheth_attach;
static device_detach_t ipheth_detach;

static usb_callback_t ipheth_bulk_write_callback;
static usb_callback_t ipheth_bulk_read_callback;

static uether_fn_t ipheth_attach_post;
static uether_fn_t ipheth_tick;
static uether_fn_t ipheth_init;
static uether_fn_t ipheth_stop;
static uether_fn_t ipheth_start;
static uether_fn_t ipheth_setmulti;
static uether_fn_t ipheth_setpromisc;

#ifdef USB_DEBUG
static int ipheth_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, ipheth, CTLFLAG_RW, 0, "USB iPhone ethernet");
SYSCTL_INT(_hw_usb_ipheth, OID_AUTO, debug, CTLFLAG_RWTUN, &ipheth_debug, 0, "Debug level");
#endif

static const struct usb_config ipheth_config[IPHETH_N_TRANSFER] = {

	[IPHETH_BULK_RX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_RX,
		.frames = IPHETH_RX_FRAMES_MAX,
		.bufsize = (IPHETH_RX_FRAMES_MAX * MCLBYTES),
		.flags = {.short_frames_ok = 1,.short_xfer_ok = 1,.ext_buffer = 1,},
		.callback = ipheth_bulk_read_callback,
		.timeout = 0,		/* no timeout */
	},

	[IPHETH_BULK_TX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.frames = IPHETH_TX_FRAMES_MAX,
		.bufsize = (IPHETH_TX_FRAMES_MAX * IPHETH_BUF_SIZE),
		.flags = {.force_short_xfer = 1,},
		.callback = ipheth_bulk_write_callback,
		.timeout = IPHETH_TX_TIMEOUT,
	},
};

static device_method_t ipheth_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, ipheth_probe),
	DEVMETHOD(device_attach, ipheth_attach),
	DEVMETHOD(device_detach, ipheth_detach),

	DEVMETHOD_END
};

static driver_t ipheth_driver = {
	.name = "ipheth",
	.methods = ipheth_methods,
	.size = sizeof(struct ipheth_softc),
};

static devclass_t ipheth_devclass;

static const STRUCT_USB_HOST_ID ipheth_devs[] = {
#if 0
	{IPHETH_ID(USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPHONE,
	    IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
	    IPHETH_USBINTF_PROTO)},
	{IPHETH_ID(USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPHONE_3G,
	    IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
	    IPHETH_USBINTF_PROTO)},
	{IPHETH_ID(USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPHONE_3GS,
	    IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
	    IPHETH_USBINTF_PROTO)},
	{IPHETH_ID(USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPHONE_4,
	    IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
	    IPHETH_USBINTF_PROTO)},
	{IPHETH_ID(USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPHONE_4S,
	    IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
	    IPHETH_USBINTF_PROTO)},
	{IPHETH_ID(USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IPHONE_5,
	    IPHETH_USBINTF_CLASS, IPHETH_USBINTF_SUBCLASS,
	    IPHETH_USBINTF_PROTO)},
#else
	/* product agnostic interface match */
	{USB_VENDOR(USB_VENDOR_APPLE),
	 USB_IFACE_CLASS(IPHETH_USBINTF_CLASS),
	 USB_IFACE_SUBCLASS(IPHETH_USBINTF_SUBCLASS),
	 USB_IFACE_PROTOCOL(IPHETH_USBINTF_PROTO)},
#endif
};

DRIVER_MODULE(ipheth, uhub, ipheth_driver, ipheth_devclass, NULL, 0);
MODULE_VERSION(ipheth, 1);
MODULE_DEPEND(ipheth, uether, 1, 1, 1);
MODULE_DEPEND(ipheth, usb, 1, 1, 1);
MODULE_DEPEND(ipheth, ether, 1, 1, 1);
USB_PNP_HOST_INFO(ipheth_devs);

static const struct usb_ether_methods ipheth_ue_methods = {
	.ue_attach_post = ipheth_attach_post,
	.ue_start = ipheth_start,
	.ue_init = ipheth_init,
	.ue_tick = ipheth_tick,
	.ue_stop = ipheth_stop,
	.ue_setmulti = ipheth_setmulti,
	.ue_setpromisc = ipheth_setpromisc,
};

#define	IPHETH_ID(v,p,c,sc,pt) \
    USB_VENDOR(v), USB_PRODUCT(p), \
    USB_IFACE_CLASS(c), USB_IFACE_SUBCLASS(sc), \
    USB_IFACE_PROTOCOL(pt)

static int
ipheth_get_mac_addr(struct ipheth_softc *sc)
{
	struct usb_device_request req;
	int error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = IPHETH_CMD_GET_MACADDR;
	req.wValue[0] = 0;
	req.wValue[1] = 0;
	req.wIndex[0] = sc->sc_iface_no;
	req.wIndex[1] = 0;
	req.wLength[0] = ETHER_ADDR_LEN;
	req.wLength[1] = 0;

	error = usbd_do_request(sc->sc_ue.ue_udev, NULL, &req, sc->sc_data);

	if (error)
		return (error);

	memcpy(sc->sc_ue.ue_eaddr, sc->sc_data, ETHER_ADDR_LEN);

	return (0);
}

static int
ipheth_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(ipheth_devs, sizeof(ipheth_devs), uaa));
}

static int
ipheth_attach(device_t dev)
{
	struct ipheth_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	int error;

	sc->sc_iface_no = uaa->info.bIfaceIndex;

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), NULL, MTX_DEF);

	error = usbd_set_alt_interface_index(uaa->device,
	    uaa->info.bIfaceIndex, IPHETH_ALT_INTFNUM);
	if (error) {
		device_printf(dev, "Cannot set alternate setting\n");
		goto detach;
	}
	error = usbd_transfer_setup(uaa->device, &sc->sc_iface_no,
	    sc->sc_xfer, ipheth_config, IPHETH_N_TRANSFER, sc, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "Cannot setup USB transfers\n");
		goto detach;
	}
	ue->ue_sc = sc;
	ue->ue_dev = dev;
	ue->ue_udev = uaa->device;
	ue->ue_mtx = &sc->sc_mtx;
	ue->ue_methods = &ipheth_ue_methods;

	error = ipheth_get_mac_addr(sc);
	if (error) {
		device_printf(dev, "Cannot get MAC address\n");
		goto detach;
	}

	error = uether_ifattach(ue);
	if (error) {
		device_printf(dev, "could not attach interface\n");
		goto detach;
	}
	return (0);			/* success */

detach:
	ipheth_detach(dev);
	return (ENXIO);			/* failure */
}

static int
ipheth_detach(device_t dev)
{
	struct ipheth_softc *sc = device_get_softc(dev);
	struct usb_ether *ue = &sc->sc_ue;

	/* stop all USB transfers first */
	usbd_transfer_unsetup(sc->sc_xfer, IPHETH_N_TRANSFER);

	uether_ifdetach(ue);

	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static void
ipheth_start(struct usb_ether *ue)
{
	struct ipheth_softc *sc = uether_getsc(ue);

	/*
	 * Start the USB transfers, if not already started:
	 */
	usbd_transfer_start(sc->sc_xfer[IPHETH_BULK_TX]);
	usbd_transfer_start(sc->sc_xfer[IPHETH_BULK_RX]);
}

static void
ipheth_stop(struct usb_ether *ue)
{
	struct ipheth_softc *sc = uether_getsc(ue);

	/*
	 * Stop the USB transfers, if not already stopped:
	 */
	usbd_transfer_stop(sc->sc_xfer[IPHETH_BULK_TX]);
	usbd_transfer_stop(sc->sc_xfer[IPHETH_BULK_RX]);
}

static void
ipheth_tick(struct usb_ether *ue)
{
	struct ipheth_softc *sc = uether_getsc(ue);
	struct usb_device_request req;
	int error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = IPHETH_CMD_CARRIER_CHECK;
	req.wValue[0] = 0;
	req.wValue[1] = 0;
	req.wIndex[0] = sc->sc_iface_no;
	req.wIndex[1] = 0;
	req.wLength[0] = IPHETH_CTRL_BUF_SIZE;
	req.wLength[1] = 0;

	error = uether_do_request(ue, &req, sc->sc_data, IPHETH_CTRL_TIMEOUT);

	if (error)
		return;

	sc->sc_carrier_on =
	    (sc->sc_data[0] == IPHETH_CARRIER_ON);
}

static void
ipheth_attach_post(struct usb_ether *ue)
{

}

static void
ipheth_init(struct usb_ether *ue)
{
	struct ipheth_softc *sc = uether_getsc(ue);
	struct ifnet *ifp = uether_getifp(ue);

	IPHETH_LOCK_ASSERT(sc, MA_OWNED);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;

	/* stall data write direction, which depends on USB mode */
	usbd_xfer_set_stall(sc->sc_xfer[IPHETH_BULK_TX]);

	/* start data transfers */
	ipheth_start(ue);
}

static void
ipheth_setmulti(struct usb_ether *ue)
{

}

static void
ipheth_setpromisc(struct usb_ether *ue)
{

}

static void
ipheth_free_queue(struct mbuf **ppm, uint8_t n)
{
	uint8_t x;

	for (x = 0; x != n; x++) {
		if (ppm[x] != NULL) {
			m_freem(ppm[x]);
			ppm[x] = NULL;
		}
	}
}

static void
ipheth_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ipheth_softc *sc = usbd_xfer_softc(xfer);
	struct ifnet *ifp = uether_getifp(&sc->sc_ue);
	struct usb_page_cache *pc;
	struct mbuf *m;
	uint8_t x;
	int actlen;
	int aframes;

	usbd_xfer_status(xfer, &actlen, NULL, &aframes, NULL);

	DPRINTFN(1, "\n");

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(11, "transfer complete: %u bytes in %u frames\n",
		    actlen, aframes);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);

		/* free all previous TX buffers */
		ipheth_free_queue(sc->sc_tx_buf, IPHETH_TX_FRAMES_MAX);

		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		for (x = 0; x != IPHETH_TX_FRAMES_MAX; x++) {

			IFQ_DRV_DEQUEUE(&ifp->if_snd, m);

			if (m == NULL)
				break;

			usbd_xfer_set_frame_offset(xfer,
			    x * IPHETH_BUF_SIZE, x);

			pc = usbd_xfer_get_frame(xfer, x);

			sc->sc_tx_buf[x] = m;

			if (m->m_pkthdr.len > IPHETH_BUF_SIZE)
				m->m_pkthdr.len = IPHETH_BUF_SIZE;

			usbd_m_copy_in(pc, 0, m, 0, m->m_pkthdr.len);

			usbd_xfer_set_frame_len(xfer, x, IPHETH_BUF_SIZE);

			if (IPHETH_BUF_SIZE != m->m_pkthdr.len) {
				usbd_frame_zero(pc, m->m_pkthdr.len,
					IPHETH_BUF_SIZE - m->m_pkthdr.len);
			}

			/*
			 * If there's a BPF listener, bounce a copy of
			 * this frame to him:
			 */
			BPF_MTAP(ifp, m);
		}
		if (x != 0) {
			usbd_xfer_set_frames(xfer, x);

			usbd_transfer_submit(xfer);
		}
		break;

	default:			/* Error */
		DPRINTFN(11, "transfer error, %s\n",
		    usbd_errstr(error));

		/* free all previous TX buffers */
		ipheth_free_queue(sc->sc_tx_buf, IPHETH_TX_FRAMES_MAX);

		/* count output errors */
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
ipheth_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ipheth_softc *sc = usbd_xfer_softc(xfer);
	struct mbuf *m;
	uint8_t x;
	int actlen;
	int aframes;
	int len;

	usbd_xfer_status(xfer, &actlen, NULL, &aframes, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		DPRINTF("received %u bytes in %u frames\n", actlen, aframes);

		for (x = 0; x != aframes; x++) {

			m = sc->sc_rx_buf[x];
			sc->sc_rx_buf[x] = NULL;
			len = usbd_xfer_frame_len(xfer, x);

			if (len < (int)(sizeof(struct ether_header) +
			    IPHETH_RX_ADJ)) {
				m_freem(m);
				continue;
			}

			m_adj(m, IPHETH_RX_ADJ);

			/* queue up mbuf */
			uether_rxmbuf(&sc->sc_ue, m, len - IPHETH_RX_ADJ);
		}

		/* FALLTHROUGH */
	case USB_ST_SETUP:

		for (x = 0; x != IPHETH_RX_FRAMES_MAX; x++) {
			if (sc->sc_rx_buf[x] == NULL) {
				m = uether_newbuf();
				if (m == NULL)
					goto tr_stall;

				/* cancel alignment for ethernet */
				m_adj(m, ETHER_ALIGN);

				sc->sc_rx_buf[x] = m;
			} else {
				m = sc->sc_rx_buf[x];
			}

			usbd_xfer_set_frame_data(xfer, x, m->m_data, m->m_len);
		}
		/* set number of frames and start hardware */
		usbd_xfer_set_frames(xfer, x);
		usbd_transfer_submit(xfer);
		/* flush any received frames */
		uether_rxflush(&sc->sc_ue);
		break;

	default:			/* Error */
		DPRINTF("error = %s\n", usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
	tr_stall:
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			usbd_xfer_set_frames(xfer, 0);
			usbd_transfer_submit(xfer);
			break;
		}
		/* need to free the RX-mbufs when we are cancelled */
		ipheth_free_queue(sc->sc_rx_buf, IPHETH_RX_FRAMES_MAX);
		break;
	}
}
