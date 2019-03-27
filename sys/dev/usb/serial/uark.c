/*	$OpenBSD: uark.c,v 1.1 2006/08/14 08:30:22 jsg Exp $	*/

/*
 * Copyright (c) 2006 Jonathan Gray <jsg@openbsd.org>
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
 *
 * $FreeBSD$
 */

/*
 * NOTE: all function names beginning like "uark_cfg_" can only
 * be called from within the config thread function !
 */


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
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbhid.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR usb_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/serial/usb_serial.h>

#define	UARK_BUF_SIZE		1024	/* bytes */

#define	UARK_SET_DATA_BITS(x)	((x) - 5)

#define	UARK_PARITY_NONE	0x00
#define	UARK_PARITY_ODD		0x08
#define	UARK_PARITY_EVEN	0x18

#define	UARK_STOP_BITS_1	0x00
#define	UARK_STOP_BITS_2	0x04

#define	UARK_BAUD_REF		3000000

#define	UARK_WRITE		0x40
#define	UARK_READ		0xc0

#define	UARK_REQUEST		0xfe

#define	UARK_CONFIG_INDEX	0
#define	UARK_IFACE_INDEX	0

enum {
	UARK_BULK_DT_WR,
	UARK_BULK_DT_RD,
	UARK_N_TRANSFER,
};

struct uark_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom;

	struct usb_xfer *sc_xfer[UARK_N_TRANSFER];
	struct usb_device *sc_udev;
	struct mtx sc_mtx;

	uint8_t	sc_msr;
	uint8_t	sc_lsr;
};

/* prototypes */

static device_probe_t uark_probe;
static device_attach_t uark_attach;
static device_detach_t uark_detach;
static void uark_free_softc(struct uark_softc *);

static usb_callback_t uark_bulk_write_callback;
static usb_callback_t uark_bulk_read_callback;

static void	uark_free(struct ucom_softc *);
static void	uark_start_read(struct ucom_softc *);
static void	uark_stop_read(struct ucom_softc *);
static void	uark_start_write(struct ucom_softc *);
static void	uark_stop_write(struct ucom_softc *);
static int	uark_pre_param(struct ucom_softc *, struct termios *);
static void	uark_cfg_param(struct ucom_softc *, struct termios *);
static void	uark_cfg_get_status(struct ucom_softc *, uint8_t *,
		    uint8_t *);
static void	uark_cfg_set_break(struct ucom_softc *, uint8_t);
static void	uark_cfg_write(struct uark_softc *, uint16_t, uint16_t);
static void	uark_poll(struct ucom_softc *ucom);

static const struct usb_config
	uark_xfer_config[UARK_N_TRANSFER] = {

	[UARK_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = UARK_BUF_SIZE,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &uark_bulk_write_callback,
	},

	[UARK_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = UARK_BUF_SIZE,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &uark_bulk_read_callback,
	},
};

static const struct ucom_callback uark_callback = {
	.ucom_cfg_get_status = &uark_cfg_get_status,
	.ucom_cfg_set_break = &uark_cfg_set_break,
	.ucom_cfg_param = &uark_cfg_param,
	.ucom_pre_param = &uark_pre_param,
	.ucom_start_read = &uark_start_read,
	.ucom_stop_read = &uark_stop_read,
	.ucom_start_write = &uark_start_write,
	.ucom_stop_write = &uark_stop_write,
	.ucom_poll = &uark_poll,
	.ucom_free = &uark_free,
};

static device_method_t uark_methods[] = {
	/* Device methods */
	DEVMETHOD(device_probe, uark_probe),
	DEVMETHOD(device_attach, uark_attach),
	DEVMETHOD(device_detach, uark_detach),
	DEVMETHOD_END
};

static devclass_t uark_devclass;

static driver_t uark_driver = {
	.name = "uark",
	.methods = uark_methods,
	.size = sizeof(struct uark_softc),
};

static const STRUCT_USB_HOST_ID uark_devs[] = {
	{USB_VPI(USB_VENDOR_ARKMICRO, USB_PRODUCT_ARKMICRO_ARK3116, 0)},
};

DRIVER_MODULE(uark, uhub, uark_driver, uark_devclass, NULL, 0);
MODULE_DEPEND(uark, ucom, 1, 1, 1);
MODULE_DEPEND(uark, usb, 1, 1, 1);
MODULE_VERSION(uark, 1);
USB_PNP_HOST_INFO(uark_devs);

static int
uark_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST) {
		return (ENXIO);
	}
	if (uaa->info.bConfigIndex != 0) {
		return (ENXIO);
	}
	if (uaa->info.bIfaceIndex != UARK_IFACE_INDEX) {
		return (ENXIO);
	}
	return (usbd_lookup_id_by_uaa(uark_devs, sizeof(uark_devs), uaa));
}

static int
uark_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct uark_softc *sc = device_get_softc(dev);
	int32_t error;
	uint8_t iface_index;

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "uark", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);

	sc->sc_udev = uaa->device;

	iface_index = UARK_IFACE_INDEX;
	error = usbd_transfer_setup
	    (uaa->device, &iface_index, sc->sc_xfer,
	    uark_xfer_config, UARK_N_TRANSFER, sc, &sc->sc_mtx);

	if (error) {
		device_printf(dev, "allocating control USB "
		    "transfers failed\n");
		goto detach;
	}
	/* clear stall at first run */
	mtx_lock(&sc->sc_mtx);
	usbd_xfer_set_stall(sc->sc_xfer[UARK_BULK_DT_WR]);
	usbd_xfer_set_stall(sc->sc_xfer[UARK_BULK_DT_RD]);
	mtx_unlock(&sc->sc_mtx);

	error = ucom_attach(&sc->sc_super_ucom, &sc->sc_ucom, 1, sc,
	    &uark_callback, &sc->sc_mtx);
	if (error) {
		DPRINTF("ucom_attach failed\n");
		goto detach;
	}
	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);

	return (0);			/* success */

detach:
	uark_detach(dev);
	return (ENXIO);			/* failure */
}

static int
uark_detach(device_t dev)
{
	struct uark_softc *sc = device_get_softc(dev);

	ucom_detach(&sc->sc_super_ucom, &sc->sc_ucom);
	usbd_transfer_unsetup(sc->sc_xfer, UARK_N_TRANSFER);

	device_claim_softc(dev);

	uark_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(uark);

static void
uark_free_softc(struct uark_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
uark_free(struct ucom_softc *ucom)
{
	uark_free_softc(ucom->sc_parent);
}

static void
uark_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uark_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:
tr_setup:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (ucom_get_data(&sc->sc_ucom, pc, 0,
		    UARK_BUF_SIZE, &actlen)) {
			usbd_xfer_set_frame_len(xfer, 0, actlen);
			usbd_transfer_submit(xfer);
		}
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
uark_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uark_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		ucom_put_data(&sc->sc_ucom, pc, 0, actlen);

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
uark_start_read(struct ucom_softc *ucom)
{
	struct uark_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[UARK_BULK_DT_RD]);
}

static void
uark_stop_read(struct ucom_softc *ucom)
{
	struct uark_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[UARK_BULK_DT_RD]);
}

static void
uark_start_write(struct ucom_softc *ucom)
{
	struct uark_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[UARK_BULK_DT_WR]);
}

static void
uark_stop_write(struct ucom_softc *ucom)
{
	struct uark_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[UARK_BULK_DT_WR]);
}

static int
uark_pre_param(struct ucom_softc *ucom, struct termios *t)
{
	if ((t->c_ospeed < 300) || (t->c_ospeed > 115200))
		return (EINVAL);
	return (0);
}

static void
uark_cfg_param(struct ucom_softc *ucom, struct termios *t)
{
	struct uark_softc *sc = ucom->sc_parent;
	uint32_t speed = t->c_ospeed;
	uint16_t data;

	/*
	 * NOTE: When reverse computing the baud rate from the "data" all
	 * allowed baud rates are within 3% of the initial baud rate.
	 */
	data = (UARK_BAUD_REF + (speed / 2)) / speed;

	uark_cfg_write(sc, 3, 0x83);
	uark_cfg_write(sc, 0, data & 0xFF);
	uark_cfg_write(sc, 1, data >> 8);
	uark_cfg_write(sc, 3, 0x03);

	if (t->c_cflag & CSTOPB)
		data = UARK_STOP_BITS_2;
	else
		data = UARK_STOP_BITS_1;

	if (t->c_cflag & PARENB) {
		if (t->c_cflag & PARODD)
			data |= UARK_PARITY_ODD;
		else
			data |= UARK_PARITY_EVEN;
	} else
		data |= UARK_PARITY_NONE;

	switch (t->c_cflag & CSIZE) {
	case CS5:
		data |= UARK_SET_DATA_BITS(5);
		break;
	case CS6:
		data |= UARK_SET_DATA_BITS(6);
		break;
	case CS7:
		data |= UARK_SET_DATA_BITS(7);
		break;
	default:
	case CS8:
		data |= UARK_SET_DATA_BITS(8);
		break;
	}
	uark_cfg_write(sc, 3, 0x00);
	uark_cfg_write(sc, 3, data);
}

static void
uark_cfg_get_status(struct ucom_softc *ucom, uint8_t *lsr, uint8_t *msr)
{
	struct uark_softc *sc = ucom->sc_parent;

	/* XXX Note: sc_lsr is always zero */
	*lsr = sc->sc_lsr;
	*msr = sc->sc_msr;
}

static void
uark_cfg_set_break(struct ucom_softc *ucom, uint8_t onoff)
{
	struct uark_softc *sc = ucom->sc_parent;

	DPRINTF("onoff=%d\n", onoff);

	uark_cfg_write(sc, 4, onoff ? 0x01 : 0x00);
}

static void
uark_cfg_write(struct uark_softc *sc, uint16_t index, uint16_t value)
{
	struct usb_device_request req;
	usb_error_t err;

	req.bmRequestType = UARK_WRITE;
	req.bRequest = UARK_REQUEST;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, 0);

	err = ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, NULL, 0, 1000);
	if (err) {
		DPRINTFN(0, "device request failed, err=%s "
		    "(ignored)\n", usbd_errstr(err));
	}
}

static void
uark_poll(struct ucom_softc *ucom)
{
	struct uark_softc *sc = ucom->sc_parent;
	usbd_transfer_poll(sc->sc_xfer, UARK_N_TRANSFER);
}
