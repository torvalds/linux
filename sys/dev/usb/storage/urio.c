/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Iwasa Kazmi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This code is based on ugen.c and ulpt.c developed by Lennart Augustsson.
 * This code includes software developed by the NetBSD Foundation, Inc. and
 * its contributors.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


/*
 * 2000/3/24  added NetBSD/OpenBSD support (from Alex Nemirovsky)
 * 2000/3/07  use two bulk-pipe handles for read and write (Dirk)
 * 2000/3/06  change major number(143), and copyright header
 *            some fix for 4.0 (Dirk)
 * 2000/3/05  codes for FreeBSD 4.x - CURRENT (Thanks to Dirk-Willem van Gulik)
 * 2000/3/01  remove retry code from urioioctl()
 *            change method of bulk transfer (no interrupt)
 * 2000/2/28  small fixes for new rio_usb.h
 * 2000/2/24  first version.
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
#include <sys/conf.h>
#include <sys/fcntl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include "usbdevs.h"

#include <dev/usb/usb_ioctl.h>
#include <dev/usb/usb_generic.h>

#define	USB_DEBUG_VAR urio_debug
#include <dev/usb/usb_debug.h>

#include <dev/usb/storage/rio500_usb.h>

#ifdef USB_DEBUG
static int urio_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, urio, CTLFLAG_RW, 0, "USB urio");
SYSCTL_INT(_hw_usb_urio, OID_AUTO, debug, CTLFLAG_RWTUN,
    &urio_debug, 0, "urio debug level");
#endif

#define	URIO_T_WR     0
#define	URIO_T_RD     1
#define	URIO_T_WR_CS  2
#define	URIO_T_RD_CS  3
#define	URIO_T_MAX    4

#define	URIO_BSIZE	(1<<12)		/* bytes */
#define	URIO_IFQ_MAXLEN      2		/* units */

struct urio_softc {
	struct usb_fifo_sc sc_fifo;
	struct mtx sc_mtx;

	struct usb_device *sc_udev;
	struct usb_xfer *sc_xfer[URIO_T_MAX];

	uint8_t	sc_flags;
#define	URIO_FLAG_READ_STALL    0x01	/* read transfer stalled */
#define	URIO_FLAG_WRITE_STALL   0x02	/* write transfer stalled */

	uint8_t	sc_name[16];
};

/* prototypes */

static device_probe_t urio_probe;
static device_attach_t urio_attach;
static device_detach_t urio_detach;

static usb_callback_t urio_write_callback;
static usb_callback_t urio_write_clear_stall_callback;
static usb_callback_t urio_read_callback;
static usb_callback_t urio_read_clear_stall_callback;

static usb_fifo_close_t urio_close;
static usb_fifo_cmd_t urio_start_read;
static usb_fifo_cmd_t urio_start_write;
static usb_fifo_cmd_t urio_stop_read;
static usb_fifo_cmd_t urio_stop_write;
static usb_fifo_ioctl_t urio_ioctl;
static usb_fifo_open_t urio_open;

static struct usb_fifo_methods urio_fifo_methods = {
	.f_close = &urio_close,
	.f_ioctl = &urio_ioctl,
	.f_open = &urio_open,
	.f_start_read = &urio_start_read,
	.f_start_write = &urio_start_write,
	.f_stop_read = &urio_stop_read,
	.f_stop_write = &urio_stop_write,
	.basename[0] = "urio",
};

static const struct usb_config urio_config[URIO_T_MAX] = {
	[URIO_T_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = URIO_BSIZE,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,.proxy_buffer = 1,},
		.callback = &urio_write_callback,
	},

	[URIO_T_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = URIO_BSIZE,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,.proxy_buffer = 1,},
		.callback = &urio_read_callback,
	},

	[URIO_T_WR_CS] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &urio_write_clear_stall_callback,
		.timeout = 1000,	/* 1 second */
		.interval = 50,	/* 50ms */
	},

	[URIO_T_RD_CS] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &urio_read_clear_stall_callback,
		.timeout = 1000,	/* 1 second */
		.interval = 50,	/* 50ms */
	},
};

static devclass_t urio_devclass;

static device_method_t urio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, urio_probe),
	DEVMETHOD(device_attach, urio_attach),
	DEVMETHOD(device_detach, urio_detach),

	DEVMETHOD_END
};

static driver_t urio_driver = {
	.name = "urio",
	.methods = urio_methods,
	.size = sizeof(struct urio_softc),
};

static const STRUCT_USB_HOST_ID urio_devs[] = {
	{USB_VPI(USB_VENDOR_DIAMOND, USB_PRODUCT_DIAMOND_RIO500USB, 0)},
	{USB_VPI(USB_VENDOR_DIAMOND2, USB_PRODUCT_DIAMOND2_RIO600USB, 0)},
	{USB_VPI(USB_VENDOR_DIAMOND2, USB_PRODUCT_DIAMOND2_RIO800USB, 0)},
};

DRIVER_MODULE(urio, uhub, urio_driver, urio_devclass, NULL, 0);
MODULE_DEPEND(urio, usb, 1, 1, 1);
MODULE_VERSION(urio, 1);
USB_PNP_HOST_INFO(urio_devs);

static int
urio_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != 0)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != 0)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(urio_devs, sizeof(urio_devs), uaa));
}

static int
urio_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct urio_softc *sc = device_get_softc(dev);
	int error;

	device_set_usb_desc(dev);

	sc->sc_udev = uaa->device;

	mtx_init(&sc->sc_mtx, "urio lock", NULL, MTX_DEF | MTX_RECURSE);

	snprintf(sc->sc_name, sizeof(sc->sc_name),
	    "%s", device_get_nameunit(dev));

	error = usbd_transfer_setup(uaa->device,
	    &uaa->info.bIfaceIndex, sc->sc_xfer,
	    urio_config, URIO_T_MAX, sc, &sc->sc_mtx);

	if (error) {
		DPRINTF("error=%s\n", usbd_errstr(error));
		goto detach;
	}

	error = usb_fifo_attach(uaa->device, sc, &sc->sc_mtx,
	    &urio_fifo_methods, &sc->sc_fifo,
	    device_get_unit(dev), -1, uaa->info.bIfaceIndex,
	    UID_ROOT, GID_OPERATOR, 0644);
	if (error) {
		goto detach;
	}
	return (0);			/* success */

detach:
	urio_detach(dev);
	return (ENOMEM);		/* failure */
}

static void
urio_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct urio_softc *sc = usbd_xfer_softc(xfer);
	struct usb_fifo *f = sc->sc_fifo.fp[USB_FIFO_TX];
	struct usb_page_cache *pc;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
	case USB_ST_SETUP:
		if (sc->sc_flags & URIO_FLAG_WRITE_STALL) {
			usbd_transfer_start(sc->sc_xfer[URIO_T_WR_CS]);
			return;
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		if (usb_fifo_get_data(f, pc, 0,
		    usbd_xfer_max_len(xfer), &actlen, 0)) {

			usbd_xfer_set_frame_len(xfer, 0, actlen);
			usbd_transfer_submit(xfer);
		}
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			sc->sc_flags |= URIO_FLAG_WRITE_STALL;
			usbd_transfer_start(sc->sc_xfer[URIO_T_WR_CS]);
		}
		return;
	}
}

static void
urio_write_clear_stall_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct urio_softc *sc = usbd_xfer_softc(xfer);
	struct usb_xfer *xfer_other = sc->sc_xfer[URIO_T_WR];

	if (usbd_clear_stall_callback(xfer, xfer_other)) {
		DPRINTF("stall cleared\n");
		sc->sc_flags &= ~URIO_FLAG_WRITE_STALL;
		usbd_transfer_start(xfer_other);
	}
}

static void
urio_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct urio_softc *sc = usbd_xfer_softc(xfer);
	struct usb_fifo *f = sc->sc_fifo.fp[USB_FIFO_RX];
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		usb_fifo_put_data(f, pc, 0, actlen, 1);

	case USB_ST_SETUP:
		if (sc->sc_flags & URIO_FLAG_READ_STALL) {
			usbd_transfer_start(sc->sc_xfer[URIO_T_RD_CS]);
			return;
		}
		if (usb_fifo_put_bytes_max(f) != 0) {
			usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
			usbd_transfer_submit(xfer);
		}
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			sc->sc_flags |= URIO_FLAG_READ_STALL;
			usbd_transfer_start(sc->sc_xfer[URIO_T_RD_CS]);
		}
		return;
	}
}

static void
urio_read_clear_stall_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct urio_softc *sc = usbd_xfer_softc(xfer);
	struct usb_xfer *xfer_other = sc->sc_xfer[URIO_T_RD];

	if (usbd_clear_stall_callback(xfer, xfer_other)) {
		DPRINTF("stall cleared\n");
		sc->sc_flags &= ~URIO_FLAG_READ_STALL;
		usbd_transfer_start(xfer_other);
	}
}

static void
urio_start_read(struct usb_fifo *fifo)
{
	struct urio_softc *sc = usb_fifo_softc(fifo);

	usbd_transfer_start(sc->sc_xfer[URIO_T_RD]);
}

static void
urio_stop_read(struct usb_fifo *fifo)
{
	struct urio_softc *sc = usb_fifo_softc(fifo);

	usbd_transfer_stop(sc->sc_xfer[URIO_T_RD_CS]);
	usbd_transfer_stop(sc->sc_xfer[URIO_T_RD]);
}

static void
urio_start_write(struct usb_fifo *fifo)
{
	struct urio_softc *sc = usb_fifo_softc(fifo);

	usbd_transfer_start(sc->sc_xfer[URIO_T_WR]);
}

static void
urio_stop_write(struct usb_fifo *fifo)
{
	struct urio_softc *sc = usb_fifo_softc(fifo);

	usbd_transfer_stop(sc->sc_xfer[URIO_T_WR_CS]);
	usbd_transfer_stop(sc->sc_xfer[URIO_T_WR]);
}

static int
urio_open(struct usb_fifo *fifo, int fflags)
{
	struct urio_softc *sc = usb_fifo_softc(fifo);

	if (fflags & FREAD) {
		/* clear stall first */
		mtx_lock(&sc->sc_mtx);
		sc->sc_flags |= URIO_FLAG_READ_STALL;
		mtx_unlock(&sc->sc_mtx);

		if (usb_fifo_alloc_buffer(fifo,
		    usbd_xfer_max_len(sc->sc_xfer[URIO_T_RD]),
		    URIO_IFQ_MAXLEN)) {
			return (ENOMEM);
		}
	}
	if (fflags & FWRITE) {
		/* clear stall first */
		sc->sc_flags |= URIO_FLAG_WRITE_STALL;

		if (usb_fifo_alloc_buffer(fifo,
		    usbd_xfer_max_len(sc->sc_xfer[URIO_T_WR]),
		    URIO_IFQ_MAXLEN)) {
			return (ENOMEM);
		}
	}
	return (0);			/* success */
}

static void
urio_close(struct usb_fifo *fifo, int fflags)
{
	if (fflags & (FREAD | FWRITE)) {
		usb_fifo_free_buffer(fifo);
	}
}

static int
urio_ioctl(struct usb_fifo *fifo, u_long cmd, void *addr,
    int fflags)
{
	struct usb_ctl_request ur;
	struct RioCommand *rio_cmd;
	int error;

	switch (cmd) {
	case RIO_RECV_COMMAND:
		if (!(fflags & FWRITE)) {
			error = EPERM;
			goto done;
		}
		memset(&ur, 0, sizeof(ur));
		rio_cmd = addr;
		ur.ucr_request.bmRequestType =
		    rio_cmd->requesttype | UT_READ_VENDOR_DEVICE;
		break;

	case RIO_SEND_COMMAND:
		if (!(fflags & FWRITE)) {
			error = EPERM;
			goto done;
		}
		memset(&ur, 0, sizeof(ur));
		rio_cmd = addr;
		ur.ucr_request.bmRequestType =
		    rio_cmd->requesttype | UT_WRITE_VENDOR_DEVICE;
		break;

	default:
		error = EINVAL;
		goto done;
	}

	DPRINTFN(2, "Sending command\n");

	/* Send rio control message */
	ur.ucr_request.bRequest = rio_cmd->request;
	USETW(ur.ucr_request.wValue, rio_cmd->value);
	USETW(ur.ucr_request.wIndex, rio_cmd->index);
	USETW(ur.ucr_request.wLength, rio_cmd->length);
	ur.ucr_data = rio_cmd->buffer;

	/* reuse generic USB code */
	error = ugen_do_request(fifo, &ur);

done:
	return (error);
}

static int
urio_detach(device_t dev)
{
	struct urio_softc *sc = device_get_softc(dev);

	DPRINTF("\n");

	usb_fifo_detach(&sc->sc_fifo);

	usbd_transfer_unsetup(sc->sc_xfer, URIO_T_MAX);

	mtx_destroy(&sc->sc_mtx);

	return (0);
}
