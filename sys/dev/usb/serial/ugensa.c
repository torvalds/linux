/* $FreeBSD$ */
/*	$NetBSD: ugensa.c,v 1.9.2.1 2007/03/24 14:55:50 yamt Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2004, 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Roland C. Dowdeswell <elric@netbsd.org>.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * NOTE: all function names beginning like "ugensa_cfg_" can only
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
#include "usbdevs.h"

#define	USB_DEBUG_VAR usb_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/serial/usb_serial.h>

#define	UGENSA_BUF_SIZE		2048	/* bytes */
#define	UGENSA_CONFIG_INDEX	0
#define	UGENSA_IFACE_INDEX	0
#define	UGENSA_IFACE_MAX	8	/* exclusivly */

enum {
	UGENSA_BULK_DT_WR,
	UGENSA_BULK_DT_RD,
	UGENSA_N_TRANSFER,
};

struct ugensa_sub_softc {
	struct ucom_softc *sc_ucom_ptr;
	struct usb_xfer *sc_xfer[UGENSA_N_TRANSFER];
};

struct ugensa_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom[UGENSA_IFACE_MAX];
	struct ugensa_sub_softc sc_sub[UGENSA_IFACE_MAX];

	struct mtx sc_mtx;
	uint8_t	sc_niface;
};

/* prototypes */

static device_probe_t ugensa_probe;
static device_attach_t ugensa_attach;
static device_detach_t ugensa_detach;
static void ugensa_free_softc(struct ugensa_softc *);

static usb_callback_t ugensa_bulk_write_callback;
static usb_callback_t ugensa_bulk_read_callback;

static void	ugensa_free(struct ucom_softc *);
static void	ugensa_start_read(struct ucom_softc *);
static void	ugensa_stop_read(struct ucom_softc *);
static void	ugensa_start_write(struct ucom_softc *);
static void	ugensa_stop_write(struct ucom_softc *);
static void	ugensa_poll(struct ucom_softc *ucom);

static const struct usb_config ugensa_xfer_config[UGENSA_N_TRANSFER] = {

	[UGENSA_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = UGENSA_BUF_SIZE,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &ugensa_bulk_write_callback,
	},

	[UGENSA_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = UGENSA_BUF_SIZE,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &ugensa_bulk_read_callback,
	},
};

static const struct ucom_callback ugensa_callback = {
	.ucom_start_read = &ugensa_start_read,
	.ucom_stop_read = &ugensa_stop_read,
	.ucom_start_write = &ugensa_start_write,
	.ucom_stop_write = &ugensa_stop_write,
	.ucom_poll = &ugensa_poll,
	.ucom_free = &ugensa_free,
};

static device_method_t ugensa_methods[] = {
	/* Device methods */
	DEVMETHOD(device_probe, ugensa_probe),
	DEVMETHOD(device_attach, ugensa_attach),
	DEVMETHOD(device_detach, ugensa_detach),
	DEVMETHOD_END
};

static devclass_t ugensa_devclass;

static driver_t ugensa_driver = {
	.name = "ugensa",
	.methods = ugensa_methods,
	.size = sizeof(struct ugensa_softc),
};

static const STRUCT_USB_HOST_ID ugensa_devs[] = {
	{USB_VPI(USB_VENDOR_AIRPRIME, USB_PRODUCT_AIRPRIME_PC5220, 0)},
	{USB_VPI(USB_VENDOR_CMOTECH, USB_PRODUCT_CMOTECH_CDMA_MODEM1, 0)},
	{USB_VPI(USB_VENDOR_KYOCERA2, USB_PRODUCT_KYOCERA2_CDMA_MSM_K, 0)},
	{USB_VPI(USB_VENDOR_HP, USB_PRODUCT_HP_49GPLUS, 0)},
	{USB_VPI(USB_VENDOR_NOVATEL2, USB_PRODUCT_NOVATEL2_FLEXPACKGPS, 0)},
};

DRIVER_MODULE(ugensa, uhub, ugensa_driver, ugensa_devclass, NULL, 0);
MODULE_DEPEND(ugensa, ucom, 1, 1, 1);
MODULE_DEPEND(ugensa, usb, 1, 1, 1);
MODULE_VERSION(ugensa, 1);
USB_PNP_HOST_INFO(ugensa_devs);

static int
ugensa_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST) {
		return (ENXIO);
	}
	if (uaa->info.bConfigIndex != UGENSA_CONFIG_INDEX) {
		return (ENXIO);
	}
	if (uaa->info.bIfaceIndex != 0) {
		return (ENXIO);
	}
	return (usbd_lookup_id_by_uaa(ugensa_devs, sizeof(ugensa_devs), uaa));
}

static int
ugensa_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct ugensa_softc *sc = device_get_softc(dev);
	struct ugensa_sub_softc *ssc;
	struct usb_interface *iface;
	int32_t error;
	uint8_t iface_index;
	int x, cnt;

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "ugensa", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);

	/* Figure out how many interfaces this device has got */
	for (cnt = 0; cnt < UGENSA_IFACE_MAX; cnt++) {
		if ((usbd_get_endpoint(uaa->device, cnt, ugensa_xfer_config + 0) == NULL) ||
		    (usbd_get_endpoint(uaa->device, cnt, ugensa_xfer_config + 1) == NULL)) {
			/* we have reached the end */
			break;
		}
	}

	if (cnt == 0) {
		device_printf(dev, "No interfaces\n");
		goto detach;
	}
	for (x = 0; x < cnt; x++) {
		iface = usbd_get_iface(uaa->device, x);
		if (iface->idesc->bInterfaceClass != UICLASS_VENDOR)
			/* Not a serial port, most likely a SD reader */
			continue;

		ssc = sc->sc_sub + sc->sc_niface;
		ssc->sc_ucom_ptr = sc->sc_ucom + sc->sc_niface;

		iface_index = (UGENSA_IFACE_INDEX + x);
		error = usbd_transfer_setup(uaa->device,
		    &iface_index, ssc->sc_xfer, ugensa_xfer_config,
		    UGENSA_N_TRANSFER, ssc, &sc->sc_mtx);

		if (error) {
			device_printf(dev, "allocating USB "
			    "transfers failed\n");
			goto detach;
		}
		/* clear stall at first run */
		mtx_lock(&sc->sc_mtx);
		usbd_xfer_set_stall(ssc->sc_xfer[UGENSA_BULK_DT_WR]);
		usbd_xfer_set_stall(ssc->sc_xfer[UGENSA_BULK_DT_RD]);
		mtx_unlock(&sc->sc_mtx);

		/* initialize port number */
		ssc->sc_ucom_ptr->sc_portno = sc->sc_niface;
		sc->sc_niface++;
		if (x != uaa->info.bIfaceIndex)
			usbd_set_parent_iface(uaa->device, x,
			    uaa->info.bIfaceIndex);
	}
	device_printf(dev, "Found %d interfaces.\n", sc->sc_niface);

	error = ucom_attach(&sc->sc_super_ucom, sc->sc_ucom, sc->sc_niface, sc,
	    &ugensa_callback, &sc->sc_mtx);
	if (error) {
		DPRINTF("attach failed\n");
		goto detach;
	}
	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);

	return (0);			/* success */

detach:
	ugensa_detach(dev);
	return (ENXIO);			/* failure */
}

static int
ugensa_detach(device_t dev)
{
	struct ugensa_softc *sc = device_get_softc(dev);
	uint8_t x;

	ucom_detach(&sc->sc_super_ucom, sc->sc_ucom);

	for (x = 0; x < sc->sc_niface; x++) {
		usbd_transfer_unsetup(sc->sc_sub[x].sc_xfer, UGENSA_N_TRANSFER);
	}

	device_claim_softc(dev);

	ugensa_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(ugensa);

static void
ugensa_free_softc(struct ugensa_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
ugensa_free(struct ucom_softc *ucom)
{
	ugensa_free_softc(ucom->sc_parent);
}

static void
ugensa_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ugensa_sub_softc *ssc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:
tr_setup:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (ucom_get_data(ssc->sc_ucom_ptr, pc, 0,
		    UGENSA_BUF_SIZE, &actlen)) {
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
ugensa_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ugensa_sub_softc *ssc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		ucom_put_data(ssc->sc_ucom_ptr, pc, 0, actlen);

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
ugensa_start_read(struct ucom_softc *ucom)
{
	struct ugensa_softc *sc = ucom->sc_parent;
	struct ugensa_sub_softc *ssc = sc->sc_sub + ucom->sc_portno;

	usbd_transfer_start(ssc->sc_xfer[UGENSA_BULK_DT_RD]);
}

static void
ugensa_stop_read(struct ucom_softc *ucom)
{
	struct ugensa_softc *sc = ucom->sc_parent;
	struct ugensa_sub_softc *ssc = sc->sc_sub + ucom->sc_portno;

	usbd_transfer_stop(ssc->sc_xfer[UGENSA_BULK_DT_RD]);
}

static void
ugensa_start_write(struct ucom_softc *ucom)
{
	struct ugensa_softc *sc = ucom->sc_parent;
	struct ugensa_sub_softc *ssc = sc->sc_sub + ucom->sc_portno;

	usbd_transfer_start(ssc->sc_xfer[UGENSA_BULK_DT_WR]);
}

static void
ugensa_stop_write(struct ucom_softc *ucom)
{
	struct ugensa_softc *sc = ucom->sc_parent;
	struct ugensa_sub_softc *ssc = sc->sc_sub + ucom->sc_portno;

	usbd_transfer_stop(ssc->sc_xfer[UGENSA_BULK_DT_WR]);
}

static void
ugensa_poll(struct ucom_softc *ucom)
{
	struct ugensa_softc *sc = ucom->sc_parent;
	struct ugensa_sub_softc *ssc = sc->sc_sub + ucom->sc_portno;

	usbd_transfer_poll(ssc->sc_xfer, UGENSA_N_TRANSFER);
}
