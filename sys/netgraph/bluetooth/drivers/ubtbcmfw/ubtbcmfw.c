/*
 * ubtbcmfw.c
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2009 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ubtbcmfw.c,v 1.3 2003/10/10 19:15:08 max Exp $
 * $FreeBSD$
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

#include "usbdevs.h"
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usb_ioctl.h>

#define	USB_DEBUG_VAR usb_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_dev.h>

/*
 * Download firmware to BCM2033.
 */

#define	UBTBCMFW_CONFIG_NO	1	/* Config number */
#define	UBTBCMFW_IFACE_IDX	0	/* Control interface */

#define	UBTBCMFW_BSIZE		1024
#define	UBTBCMFW_IFQ_MAXLEN	2

enum {
	UBTBCMFW_BULK_DT_WR = 0,
	UBTBCMFW_INTR_DT_RD,
	UBTBCMFW_N_TRANSFER,
};

struct ubtbcmfw_softc {
	struct usb_device	*sc_udev;
	struct mtx		sc_mtx;
	struct usb_xfer	*sc_xfer[UBTBCMFW_N_TRANSFER];
	struct usb_fifo_sc	sc_fifo;
};

/*
 * Prototypes
 */

static device_probe_t		ubtbcmfw_probe;
static device_attach_t		ubtbcmfw_attach;
static device_detach_t		ubtbcmfw_detach;

static usb_callback_t		ubtbcmfw_write_callback;
static usb_callback_t		ubtbcmfw_read_callback;

static usb_fifo_close_t	ubtbcmfw_close;
static usb_fifo_cmd_t		ubtbcmfw_start_read;
static usb_fifo_cmd_t		ubtbcmfw_start_write;
static usb_fifo_cmd_t		ubtbcmfw_stop_read;
static usb_fifo_cmd_t		ubtbcmfw_stop_write;
static usb_fifo_ioctl_t	ubtbcmfw_ioctl;
static usb_fifo_open_t		ubtbcmfw_open;

static struct usb_fifo_methods	ubtbcmfw_fifo_methods = 
{
	.f_close =		&ubtbcmfw_close,
	.f_ioctl =		&ubtbcmfw_ioctl,
	.f_open =		&ubtbcmfw_open,
	.f_start_read =		&ubtbcmfw_start_read,
	.f_start_write =	&ubtbcmfw_start_write,
	.f_stop_read =		&ubtbcmfw_stop_read,
	.f_stop_write =		&ubtbcmfw_stop_write,
	.basename[0] =		"ubtbcmfw",
	.basename[1] =		"ubtbcmfw",
	.basename[2] =		"ubtbcmfw",
	.postfix[0] =		"",
	.postfix[1] =		".1",
	.postfix[2] =		".2",
};

/*
 * Device's config structure
 */

static const struct usb_config	ubtbcmfw_config[UBTBCMFW_N_TRANSFER] =
{
	[UBTBCMFW_BULK_DT_WR] = {
		.type =		UE_BULK,
		.endpoint =	0x02,	/* fixed */
		.direction =	UE_DIR_OUT,
		.if_index =	UBTBCMFW_IFACE_IDX,
		.bufsize =	UBTBCMFW_BSIZE,
		.flags =	{ .pipe_bof = 1, .force_short_xfer = 1,
				  .proxy_buffer = 1, },
		.callback =	&ubtbcmfw_write_callback,
	},

	[UBTBCMFW_INTR_DT_RD] = {
		.type =		UE_INTERRUPT,
		.endpoint =	0x01,	/* fixed */
		.direction =	UE_DIR_IN,
		.if_index =	UBTBCMFW_IFACE_IDX,
		.bufsize =	UBTBCMFW_BSIZE,
		.flags =	{ .pipe_bof = 1, .short_xfer_ok = 1,
				  .proxy_buffer = 1, },
		.callback =	&ubtbcmfw_read_callback,
	},
};

/*
 * Module
 */

static devclass_t	ubtbcmfw_devclass;

static device_method_t	ubtbcmfw_methods[] =
{
	DEVMETHOD(device_probe, ubtbcmfw_probe),
	DEVMETHOD(device_attach, ubtbcmfw_attach),
	DEVMETHOD(device_detach, ubtbcmfw_detach),
	{0, 0}
};

static driver_t		ubtbcmfw_driver =
{
	.name =		"ubtbcmfw",
	.methods =	ubtbcmfw_methods,
	.size =		sizeof(struct ubtbcmfw_softc),
};

static const STRUCT_USB_HOST_ID ubtbcmfw_devs[] = {
/* Broadcom BCM2033 devices only */
	{ USB_VPI(USB_VENDOR_BROADCOM, USB_PRODUCT_BROADCOM_BCM2033, 0) },
};


DRIVER_MODULE(ubtbcmfw, uhub, ubtbcmfw_driver, ubtbcmfw_devclass, NULL, 0);
MODULE_DEPEND(ubtbcmfw, usb, 1, 1, 1);
USB_PNP_HOST_INFO(ubtbcmfw_devs);

/*
 * Probe for a USB Bluetooth device
 */

static int
ubtbcmfw_probe(device_t dev)
{
	struct usb_attach_arg	*uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);

	if (uaa->info.bIfaceIndex != 0)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(ubtbcmfw_devs, sizeof(ubtbcmfw_devs), uaa));
} /* ubtbcmfw_probe */

/*
 * Attach the device
 */

static int
ubtbcmfw_attach(device_t dev)
{
	struct usb_attach_arg	*uaa = device_get_ivars(dev);
	struct ubtbcmfw_softc	*sc = device_get_softc(dev);
	uint8_t			iface_index;
	int			error;

	sc->sc_udev = uaa->device;

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, "ubtbcmfw lock", NULL, MTX_DEF | MTX_RECURSE);

	iface_index = UBTBCMFW_IFACE_IDX;
	error = usbd_transfer_setup(uaa->device, &iface_index, sc->sc_xfer,
				ubtbcmfw_config, UBTBCMFW_N_TRANSFER,
				sc, &sc->sc_mtx);
	if (error != 0) {
		device_printf(dev, "allocating USB transfers failed. %s\n",
			usbd_errstr(error));
		goto detach;
	}

	error = usb_fifo_attach(uaa->device, sc, &sc->sc_mtx,
			&ubtbcmfw_fifo_methods, &sc->sc_fifo,
			device_get_unit(dev), 0 - 1, uaa->info.bIfaceIndex,
			UID_ROOT, GID_OPERATOR, 0644);
	if (error != 0) {
		device_printf(dev, "could not attach fifo. %s\n",
			usbd_errstr(error));
		goto detach;
	}

	return (0);	/* success */

detach:
	ubtbcmfw_detach(dev);

	return (ENXIO);	/* failure */
} /* ubtbcmfw_attach */ 

/*
 * Detach the device
 */

static int
ubtbcmfw_detach(device_t dev)
{
	struct ubtbcmfw_softc	*sc = device_get_softc(dev);

	usb_fifo_detach(&sc->sc_fifo);

	usbd_transfer_unsetup(sc->sc_xfer, UBTBCMFW_N_TRANSFER);

	mtx_destroy(&sc->sc_mtx);

	return (0);
} /* ubtbcmfw_detach */

/*
 * USB write callback
 */

static void
ubtbcmfw_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ubtbcmfw_softc	*sc = usbd_xfer_softc(xfer);
	struct usb_fifo	*f = sc->sc_fifo.fp[USB_FIFO_TX];
	struct usb_page_cache	*pc;
	uint32_t		actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:
setup_next:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (usb_fifo_get_data(f, pc, 0, usbd_xfer_max_len(xfer),
			    &actlen, 0)) {
			usbd_xfer_set_frame_len(xfer, 0, actlen);
			usbd_transfer_submit(xfer);
		}
		break;

	default: /* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto setup_next;
		}
		break;
	}
} /* ubtbcmfw_write_callback */

/*
 * USB read callback
 */

static void
ubtbcmfw_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ubtbcmfw_softc	*sc = usbd_xfer_softc(xfer);
	struct usb_fifo	*fifo = sc->sc_fifo.fp[USB_FIFO_RX];
	struct usb_page_cache	*pc;
	int			actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		usb_fifo_put_data(fifo, pc, 0, actlen, 1);
		/* FALLTHROUGH */

	case USB_ST_SETUP:
setup_next:
		if (usb_fifo_put_bytes_max(fifo) > 0) {
			usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
			usbd_transfer_submit(xfer);
		}
		break;

	default: /* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto setup_next;
		}
		break;
	}
} /* ubtbcmfw_read_callback */

/*
 * Called when we about to start read()ing from the device
 */

static void
ubtbcmfw_start_read(struct usb_fifo *fifo)
{
	struct ubtbcmfw_softc	*sc = usb_fifo_softc(fifo);

	usbd_transfer_start(sc->sc_xfer[UBTBCMFW_INTR_DT_RD]);
} /* ubtbcmfw_start_read */

/*
 * Called when we about to stop reading (i.e. closing fifo)
 */

static void
ubtbcmfw_stop_read(struct usb_fifo *fifo)
{
	struct ubtbcmfw_softc	*sc = usb_fifo_softc(fifo);

	usbd_transfer_stop(sc->sc_xfer[UBTBCMFW_INTR_DT_RD]);
} /* ubtbcmfw_stop_read */

/*
 * Called when we about to start write()ing to the device, poll()ing
 * for write or flushing fifo
 */

static void
ubtbcmfw_start_write(struct usb_fifo *fifo)
{
	struct ubtbcmfw_softc	*sc = usb_fifo_softc(fifo);

	usbd_transfer_start(sc->sc_xfer[UBTBCMFW_BULK_DT_WR]);
} /* ubtbcmfw_start_write */

/*
 * Called when we about to stop writing (i.e. closing fifo)
 */

static void
ubtbcmfw_stop_write(struct usb_fifo *fifo)
{
	struct ubtbcmfw_softc	*sc = usb_fifo_softc(fifo);

	usbd_transfer_stop(sc->sc_xfer[UBTBCMFW_BULK_DT_WR]);
} /* ubtbcmfw_stop_write */

/*
 * Called when fifo is open
 */

static int
ubtbcmfw_open(struct usb_fifo *fifo, int fflags)
{
	struct ubtbcmfw_softc	*sc = usb_fifo_softc(fifo);
	struct usb_xfer	*xfer;

	/*
	 * f_open fifo method can only be called with either FREAD
	 * or FWRITE flag set at one time.
	 */

	if (fflags & FREAD)
		xfer = sc->sc_xfer[UBTBCMFW_INTR_DT_RD];
	else if (fflags & FWRITE)
		xfer = sc->sc_xfer[UBTBCMFW_BULK_DT_WR];
	else
		return (EINVAL);	/* should not happen */

	if (usb_fifo_alloc_buffer(fifo, usbd_xfer_max_len(xfer),
			UBTBCMFW_IFQ_MAXLEN) != 0)
		return (ENOMEM);

	return (0);
} /* ubtbcmfw_open */

/* 
 * Called when fifo is closed
 */

static void
ubtbcmfw_close(struct usb_fifo *fifo, int fflags)
{
	if (fflags & (FREAD | FWRITE))
		usb_fifo_free_buffer(fifo);
} /* ubtbcmfw_close */

/*
 * Process ioctl() on USB device
 */

static int
ubtbcmfw_ioctl(struct usb_fifo *fifo, u_long cmd, void *data,
    int fflags)
{
	struct ubtbcmfw_softc	*sc = usb_fifo_softc(fifo);
	int			error = 0;

	switch (cmd) {
	case USB_GET_DEVICE_DESC:
		memcpy(data, usbd_get_device_descriptor(sc->sc_udev),
			sizeof(struct usb_device_descriptor));
		break;

	default:
		error = EINVAL;
		break;
	}

	return (error);
} /* ubtbcmfw_ioctl */
