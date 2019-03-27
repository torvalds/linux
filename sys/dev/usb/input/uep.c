/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2010, Gleb Smirnoff <glebius@FreeBSD.org>
 * All rights reserved.
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
 *
 * $FreeBSD$
 */

/*
 *  http://www.eeti.com.tw/pdf/Software%20Programming%20Guide_v2.0.pdf
 */

#include "opt_evdev.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbhid.h>
#include "usbdevs.h"

#ifdef EVDEV_SUPPORT
#include <dev/evdev/input.h>
#include <dev/evdev/evdev.h>
#else
#include <sys/ioccom.h>
#include <sys/fcntl.h>
#include <sys/tty.h>
#endif

#define USB_DEBUG_VAR uep_debug
#include <dev/usb/usb_debug.h>

#ifdef USB_DEBUG
static int uep_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, uep, CTLFLAG_RW, 0, "USB uep");
SYSCTL_INT(_hw_usb_uep, OID_AUTO, debug, CTLFLAG_RWTUN,
    &uep_debug, 0, "Debug level");
#endif

#define UEP_MAX_X		2047
#define UEP_MAX_Y		2047

#define UEP_DOWN		0x01
#define UEP_PACKET_LEN_MAX	16
#define UEP_PACKET_LEN_REPORT	5
#define UEP_PACKET_LEN_REPORT2	6
#define UEP_PACKET_DIAG		0x0a
#define UEP_PACKET_REPORT_MASK		0xe0
#define UEP_PACKET_REPORT		0x80
#define UEP_PACKET_REPORT_PRESSURE	0xc0
#define UEP_PACKET_REPORT_PLAYER	0xa0
#define	UEP_PACKET_LEN_MASK	

#define UEP_FIFO_BUF_SIZE	8	/* bytes */
#define UEP_FIFO_QUEUE_MAXLEN	50	/* units */

enum {
	UEP_INTR_DT,
	UEP_N_TRANSFER,
};

struct uep_softc {
	struct mtx mtx;

	struct usb_xfer *xfer[UEP_N_TRANSFER];
#ifdef EVDEV_SUPPORT
	struct evdev_dev *evdev;
#else
	struct usb_fifo_sc fifo;

	u_int		pollrate;
	u_int		state;
#define UEP_ENABLED	0x01
#endif

	/* Reassembling buffer. */
	u_char		buf[UEP_PACKET_LEN_MAX];
	uint8_t		buf_len;
};

static usb_callback_t uep_intr_callback;

static device_probe_t	uep_probe;
static device_attach_t	uep_attach;
static device_detach_t	uep_detach;

#ifdef EVDEV_SUPPORT

static evdev_open_t	uep_ev_open;
static evdev_close_t	uep_ev_close;

static const struct evdev_methods uep_evdev_methods = {
	.ev_open = &uep_ev_open,
	.ev_close = &uep_ev_close,
};

#else /* !EVDEV_SUPPORT */

static usb_fifo_cmd_t	uep_start_read;
static usb_fifo_cmd_t	uep_stop_read;
static usb_fifo_open_t	uep_open;
static usb_fifo_close_t	uep_close;

static void uep_put_queue(struct uep_softc *, u_char *);

static struct usb_fifo_methods uep_fifo_methods = {
	.f_open = &uep_open,
	.f_close = &uep_close,
	.f_start_read = &uep_start_read,
	.f_stop_read = &uep_stop_read,
	.basename[0] = "uep",
};
#endif /* !EVDEV_SUPPORT */

static int
get_pkt_len(u_char *buf)
{
	if (buf[0] == UEP_PACKET_DIAG) {
		int len;

		len = buf[1] + 2;
		if (len > UEP_PACKET_LEN_MAX) {
			DPRINTF("bad packet len %u\n", len);
			return (UEP_PACKET_LEN_MAX);
		}

		return (len);
	}

	switch (buf[0] & UEP_PACKET_REPORT_MASK) {
	case UEP_PACKET_REPORT:
		return (UEP_PACKET_LEN_REPORT);
	case UEP_PACKET_REPORT_PRESSURE:
	case UEP_PACKET_REPORT_PLAYER:
	case UEP_PACKET_REPORT_PRESSURE | UEP_PACKET_REPORT_PLAYER:
		return (UEP_PACKET_LEN_REPORT2);
	default:
		DPRINTF("bad packet len 0\n");
		return (0);
	}
}

static void
uep_process_pkt(struct uep_softc *sc, u_char *buf)
{
	int32_t x, y;
#ifdef EVDEV_SUPPORT
	int touch;
#endif

	if ((buf[0] & 0xFE) != 0x80) {
		DPRINTF("bad input packet format 0x%.2x\n", buf[0]);
		return;
	}

	/*
	 * Packet format is 5 bytes:
	 *
	 * 1000000T
	 * 0000AAAA
	 * 0AAAAAAA
	 * 0000BBBB
	 * 0BBBBBBB
	 *
	 * T: 1=touched 0=not touched
	 * A: bits of axis A position, MSB to LSB
	 * B: bits of axis B position, MSB to LSB
	 *
	 * For the unit I have, which is CTF1020-S from CarTFT.com,
	 * A = X and B = Y. But in NetBSD uep(4) it is other way round :)
	 *
	 * The controller sends a stream of T=1 events while the
	 * panel is touched, followed by a single T=0 event.
	 *
	 */

	x = (buf[1] << 7) | buf[2];
	y = (buf[3] << 7) | buf[4];

	DPRINTFN(2, "x %u y %u\n", x, y);

#ifdef EVDEV_SUPPORT
	touch = buf[0] & (1 << 0);
	if (touch) {
		evdev_push_abs(sc->evdev, ABS_X, x);
		evdev_push_abs(sc->evdev, ABS_Y, y);
	}
	evdev_push_key(sc->evdev, BTN_TOUCH, touch);
	evdev_sync(sc->evdev);
#else
	uep_put_queue(sc, buf);
#endif
}

static void
uep_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uep_softc *sc = usbd_xfer_softc(xfer);
	int len;

	usbd_xfer_status(xfer, &len, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
	    {
		struct usb_page_cache *pc;
		u_char buf[17], *p;
		int pkt_len;

		if (len > (int)sizeof(buf)) {
			DPRINTF("bad input length %d\n", len);
			goto tr_setup;
		}

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, buf, len);

		/*
		 * The below code mimics Linux a lot. I don't know
		 * why NetBSD reads complete packets, but we need
		 * to reassamble 'em like Linux does (tries?).
		 */
		if (sc->buf_len > 0) {
			int res;

			if (sc->buf_len == 1)
				sc->buf[1] = buf[0];

			if ((pkt_len = get_pkt_len(sc->buf)) == 0)
				goto tr_setup;

			res = pkt_len - sc->buf_len;
			memcpy(sc->buf + sc->buf_len, buf, res);
			uep_process_pkt(sc, sc->buf);
			sc->buf_len = 0;
 
			p = buf + res;
			len -= res;
		} else
			p = buf;

		if (len == 1) {
			sc->buf[0] = buf[0];
			sc->buf_len = 1;

			goto tr_setup;
		}

		while (len > 0) {
			if ((pkt_len = get_pkt_len(p)) == 0)
				goto tr_setup;

			/* full packet: process */
			if (pkt_len <= len) {
				uep_process_pkt(sc, p);
			} else {
				/* incomplete packet: save in buffer */
				memcpy(sc->buf, p, len);
				sc->buf_len = len;
			}
			p += pkt_len;
			len -= pkt_len;
		}
	    }
	case USB_ST_SETUP:
	tr_setup:
#ifndef EVDEV_SUPPORT
		/* check if we can put more data into the FIFO */
		if (usb_fifo_put_bytes_max(sc->fifo.fp[USB_FIFO_RX]) == 0)
			break;
#endif
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:
		if (error != USB_ERR_CANCELLED) {
			/* try clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static const struct usb_config uep_config[UEP_N_TRANSFER] = {
	[UEP_INTR_DT] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,   /* use wMaxPacketSize */
		.callback = &uep_intr_callback,
	},
};

static const STRUCT_USB_HOST_ID uep_devs[] = {
	{USB_VPI(USB_VENDOR_EGALAX, USB_PRODUCT_EGALAX_TPANEL, 0)},
	{USB_VPI(USB_VENDOR_EGALAX, USB_PRODUCT_EGALAX_TPANEL2, 0)},
	{USB_VPI(USB_VENDOR_EGALAX2, USB_PRODUCT_EGALAX2_TPANEL, 0)},
};

static int
uep_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != 0)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != 0)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(uep_devs, sizeof(uep_devs), uaa));
}

static int
uep_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct uep_softc *sc = device_get_softc(dev);
	int error;

	device_set_usb_desc(dev);

	mtx_init(&sc->mtx, "uep lock", NULL, MTX_DEF);

	error = usbd_transfer_setup(uaa->device, &uaa->info.bIfaceIndex,
	    sc->xfer, uep_config, UEP_N_TRANSFER, sc, &sc->mtx);

	if (error) {
		DPRINTF("usbd_transfer_setup error=%s\n", usbd_errstr(error));
		goto detach;
	}

#ifdef EVDEV_SUPPORT
	sc->evdev = evdev_alloc();
	evdev_set_name(sc->evdev, device_get_desc(dev));
	evdev_set_phys(sc->evdev, device_get_nameunit(dev));
	evdev_set_id(sc->evdev, BUS_USB, uaa->info.idVendor,
	    uaa->info.idProduct, 0);
	evdev_set_serial(sc->evdev, usb_get_serial(uaa->device));
	evdev_set_methods(sc->evdev, sc, &uep_evdev_methods);
	evdev_support_prop(sc->evdev, INPUT_PROP_DIRECT);
	evdev_support_event(sc->evdev, EV_SYN);
	evdev_support_event(sc->evdev, EV_ABS);
	evdev_support_event(sc->evdev, EV_KEY);
	evdev_support_key(sc->evdev, BTN_TOUCH);
	evdev_support_abs(sc->evdev, ABS_X, 0, 0, UEP_MAX_X, 0, 0, 0);
	evdev_support_abs(sc->evdev, ABS_Y, 0, 0, UEP_MAX_Y, 0, 0, 0);

	error = evdev_register_mtx(sc->evdev, &sc->mtx);
	if (error) {
		DPRINTF("evdev_register_mtx error=%s\n", usbd_errstr(error));
		goto detach;
	}
#else /* !EVDEV_SUPPORT */
	error = usb_fifo_attach(uaa->device, sc, &sc->mtx, &uep_fifo_methods,
	    &sc->fifo, device_get_unit(dev), -1, uaa->info.bIfaceIndex,
	    UID_ROOT, GID_OPERATOR, 0644);

        if (error) {
		DPRINTF("usb_fifo_attach error=%s\n", usbd_errstr(error));
                goto detach;
        }
#endif /* !EVDEV_SUPPORT */

	sc->buf_len = 0;

	return (0);	

detach:
	uep_detach(dev);

	return (ENOMEM); /* XXX */
}

static int
uep_detach(device_t dev)
{
	struct uep_softc *sc = device_get_softc(dev);

#ifdef EVDEV_SUPPORT
	evdev_free(sc->evdev);
#else
	usb_fifo_detach(&sc->fifo);
#endif

	usbd_transfer_unsetup(sc->xfer, UEP_N_TRANSFER);

	mtx_destroy(&sc->mtx);

	return (0);
}

#ifdef EVDEV_SUPPORT

static int
uep_ev_close(struct evdev_dev *evdev)
{
	struct uep_softc *sc = evdev_get_softc(evdev);

	mtx_assert(&sc->mtx, MA_OWNED);
	usbd_transfer_stop(sc->xfer[UEP_INTR_DT]);

	return (0);
}

static int
uep_ev_open(struct evdev_dev *evdev)
{
	struct uep_softc *sc = evdev_get_softc(evdev);

	mtx_assert(&sc->mtx, MA_OWNED);
	usbd_transfer_start(sc->xfer[UEP_INTR_DT]);

	return (0);
}

#else /* !EVDEV_SUPPORT */

static void
uep_start_read(struct usb_fifo *fifo)
{
	struct uep_softc *sc = usb_fifo_softc(fifo);
	u_int rate;

	if ((rate = sc->pollrate) > 1000)
		rate = 1000;

	if (rate > 0 && sc->xfer[UEP_INTR_DT] != NULL) {
		usbd_transfer_stop(sc->xfer[UEP_INTR_DT]);
		usbd_xfer_set_interval(sc->xfer[UEP_INTR_DT], 1000 / rate);
		sc->pollrate = 0;
	}

	usbd_transfer_start(sc->xfer[UEP_INTR_DT]);
}

static void
uep_stop_read(struct usb_fifo *fifo)
{
	struct uep_softc *sc = usb_fifo_softc(fifo);

	usbd_transfer_stop(sc->xfer[UEP_INTR_DT]);
}

static void
uep_put_queue(struct uep_softc *sc, u_char *buf)
{
	usb_fifo_put_data_linear(sc->fifo.fp[USB_FIFO_RX], buf,
	    UEP_PACKET_LEN_REPORT, 1);
}

static int
uep_open(struct usb_fifo *fifo, int fflags)
{
	if (fflags & FREAD) {
		struct uep_softc *sc = usb_fifo_softc(fifo);

		if (sc->state & UEP_ENABLED)
			return (EBUSY);
		if (usb_fifo_alloc_buffer(fifo, UEP_FIFO_BUF_SIZE,
		    UEP_FIFO_QUEUE_MAXLEN))
			return (ENOMEM);

		sc->state |= UEP_ENABLED;
	}

	return (0);
}

static void
uep_close(struct usb_fifo *fifo, int fflags)
{
	if (fflags & FREAD) {
		struct uep_softc *sc = usb_fifo_softc(fifo);

		sc->state &= ~(UEP_ENABLED);
		usb_fifo_free_buffer(fifo);
	}
}
#endif /* !EVDEV_SUPPORT */

static devclass_t uep_devclass;

static device_method_t uep_methods[] = {
	DEVMETHOD(device_probe, uep_probe),
       	DEVMETHOD(device_attach, uep_attach),
	DEVMETHOD(device_detach, uep_detach),
	{ 0, 0 },
};

static driver_t uep_driver = {
	.name = "uep",
	.methods = uep_methods,
	.size = sizeof(struct uep_softc),
};

DRIVER_MODULE(uep, uhub, uep_driver, uep_devclass, NULL, NULL);
MODULE_DEPEND(uep, usb, 1, 1, 1);
#ifdef EVDEV_SUPPORT
MODULE_DEPEND(uep, evdev, 1, 1, 1);
#endif
MODULE_VERSION(uep, 1);
USB_PNP_HOST_INFO(uep_devs);
