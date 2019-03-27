/*-
 * Copyright (c) 2004 Bernd Walter <ticso@FreeBSD.org>
 *
 * $URL: https://devel.bwct.de/svn/projects/ubser/ubser.c $
 * $Date: 2004-02-29 01:53:10 +0100 (Sun, 29 Feb 2004) $
 * $Author: ticso $
 * $Rev: 1127 $
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2001-2002, Shunsuke Akiyama <akiyama@jp.FreeBSD.org>.
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
 */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net).
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * BWCT serial adapter driver
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
#include "usbdevs.h"

#define	USB_DEBUG_VAR ubser_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/serial/usb_serial.h>

#define	UBSER_UNIT_MAX	32

/* Vendor Interface Requests */
#define	VENDOR_GET_NUMSER		0x01
#define	VENDOR_SET_BREAK		0x02
#define	VENDOR_CLEAR_BREAK		0x03

#ifdef USB_DEBUG
static int ubser_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, ubser, CTLFLAG_RW, 0, "USB ubser");
SYSCTL_INT(_hw_usb_ubser, OID_AUTO, debug, CTLFLAG_RWTUN,
    &ubser_debug, 0, "ubser debug level");
#endif

enum {
	UBSER_BULK_DT_WR,
	UBSER_BULK_DT_RD,
	UBSER_N_TRANSFER,
};

struct ubser_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom[UBSER_UNIT_MAX];

	struct usb_xfer *sc_xfer[UBSER_N_TRANSFER];
	struct usb_device *sc_udev;
	struct mtx sc_mtx;

	uint16_t sc_tx_size;

	uint8_t	sc_numser;
	uint8_t	sc_iface_no;
	uint8_t	sc_iface_index;
	uint8_t	sc_curr_tx_unit;
};

/* prototypes */

static device_probe_t ubser_probe;
static device_attach_t ubser_attach;
static device_detach_t ubser_detach;
static void ubser_free_softc(struct ubser_softc *);

static usb_callback_t ubser_write_callback;
static usb_callback_t ubser_read_callback;

static void	ubser_free(struct ucom_softc *);
static int	ubser_pre_param(struct ucom_softc *, struct termios *);
static void	ubser_cfg_set_break(struct ucom_softc *, uint8_t);
static void	ubser_cfg_get_status(struct ucom_softc *, uint8_t *,
		    uint8_t *);
static void	ubser_start_read(struct ucom_softc *);
static void	ubser_stop_read(struct ucom_softc *);
static void	ubser_start_write(struct ucom_softc *);
static void	ubser_stop_write(struct ucom_softc *);
static void	ubser_poll(struct ucom_softc *ucom);

static const struct usb_config ubser_config[UBSER_N_TRANSFER] = {

	[UBSER_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = 0,	/* use wMaxPacketSize */
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &ubser_write_callback,
	},

	[UBSER_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = 0,	/* use wMaxPacketSize */
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &ubser_read_callback,
	},
};

static const struct ucom_callback ubser_callback = {
	.ucom_cfg_set_break = &ubser_cfg_set_break,
	.ucom_cfg_get_status = &ubser_cfg_get_status,
	.ucom_pre_param = &ubser_pre_param,
	.ucom_start_read = &ubser_start_read,
	.ucom_stop_read = &ubser_stop_read,
	.ucom_start_write = &ubser_start_write,
	.ucom_stop_write = &ubser_stop_write,
	.ucom_poll = &ubser_poll,
	.ucom_free = &ubser_free,
};

static device_method_t ubser_methods[] = {
	DEVMETHOD(device_probe, ubser_probe),
	DEVMETHOD(device_attach, ubser_attach),
	DEVMETHOD(device_detach, ubser_detach),
	DEVMETHOD_END
};

static devclass_t ubser_devclass;

static driver_t ubser_driver = {
	.name = "ubser",
	.methods = ubser_methods,
	.size = sizeof(struct ubser_softc),
};

DRIVER_MODULE(ubser, uhub, ubser_driver, ubser_devclass, NULL, 0);
MODULE_DEPEND(ubser, ucom, 1, 1, 1);
MODULE_DEPEND(ubser, usb, 1, 1, 1);
MODULE_VERSION(ubser, 1);

static int
ubser_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST) {
		return (ENXIO);
	}
	/* check if this is a BWCT vendor specific ubser interface */
	if ((strcmp(usb_get_manufacturer(uaa->device), "BWCT") == 0) &&
	    (uaa->info.bInterfaceClass == 0xff) &&
	    (uaa->info.bInterfaceSubClass == 0x00))
		return (0);

	return (ENXIO);
}

static int
ubser_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct ubser_softc *sc = device_get_softc(dev);
	struct usb_device_request req;
	uint8_t n;
	int error;

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "ubser", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);

	sc->sc_iface_no = uaa->info.bIfaceNum;
	sc->sc_iface_index = uaa->info.bIfaceIndex;
	sc->sc_udev = uaa->device;

	/* get number of serials */
	req.bmRequestType = UT_READ_VENDOR_INTERFACE;
	req.bRequest = VENDOR_GET_NUMSER;
	USETW(req.wValue, 0);
	req.wIndex[0] = sc->sc_iface_no;
	req.wIndex[1] = 0;
	USETW(req.wLength, 1);
	error = usbd_do_request_flags(uaa->device, NULL,
	    &req, &sc->sc_numser,
	    0, NULL, USB_DEFAULT_TIMEOUT);

	if (error || (sc->sc_numser == 0)) {
		device_printf(dev, "failed to get number "
		    "of serial ports: %s\n",
		    usbd_errstr(error));
		goto detach;
	}
	if (sc->sc_numser > UBSER_UNIT_MAX)
		sc->sc_numser = UBSER_UNIT_MAX;

	device_printf(dev, "found %i serials\n", sc->sc_numser);

	error = usbd_transfer_setup(uaa->device, &sc->sc_iface_index,
	    sc->sc_xfer, ubser_config, UBSER_N_TRANSFER, sc, &sc->sc_mtx);
	if (error) {
		goto detach;
	}
	sc->sc_tx_size = usbd_xfer_max_len(sc->sc_xfer[UBSER_BULK_DT_WR]);

	if (sc->sc_tx_size == 0) {
		DPRINTFN(0, "invalid tx_size\n");
		goto detach;
	}
	/* initialize port numbers */

	for (n = 0; n < sc->sc_numser; n++) {
		sc->sc_ucom[n].sc_portno = n;
	}

	error = ucom_attach(&sc->sc_super_ucom, sc->sc_ucom,
	    sc->sc_numser, sc, &ubser_callback, &sc->sc_mtx);
	if (error) {
		goto detach;
	}
	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);

	mtx_lock(&sc->sc_mtx);
	usbd_xfer_set_stall(sc->sc_xfer[UBSER_BULK_DT_WR]);
	usbd_xfer_set_stall(sc->sc_xfer[UBSER_BULK_DT_RD]);
	usbd_transfer_start(sc->sc_xfer[UBSER_BULK_DT_RD]);
	mtx_unlock(&sc->sc_mtx);

	return (0);			/* success */

detach:
	ubser_detach(dev);
	return (ENXIO);			/* failure */
}

static int
ubser_detach(device_t dev)
{
	struct ubser_softc *sc = device_get_softc(dev);

	DPRINTF("\n");

	ucom_detach(&sc->sc_super_ucom, sc->sc_ucom);
	usbd_transfer_unsetup(sc->sc_xfer, UBSER_N_TRANSFER);

	device_claim_softc(dev);

	ubser_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(ubser);

static void
ubser_free_softc(struct ubser_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
ubser_free(struct ucom_softc *ucom)
{
	ubser_free_softc(ucom->sc_parent);
}

static int
ubser_pre_param(struct ucom_softc *ucom, struct termios *t)
{
	DPRINTF("\n");

	/*
	 * The firmware on our devices can only do 8n1@9600bps
	 * without handshake.
	 * We refuse to accept other configurations.
	 */

	/* ensure 9600bps */
	switch (t->c_ospeed) {
	case 9600:
		break;
	default:
		return (EINVAL);
	}

	/* 2 stop bits not possible */
	if (t->c_cflag & CSTOPB)
		return (EINVAL);

	/* XXX parity handling not possible with current firmware */
	if (t->c_cflag & PARENB)
		return (EINVAL);

	/* we can only do 8 data bits */
	switch (t->c_cflag & CSIZE) {
	case CS8:
		break;
	default:
		return (EINVAL);
	}

	/* we can't do any kind of hardware handshaking */
	if ((t->c_cflag &
	    (CRTS_IFLOW | CDTR_IFLOW | CDSR_OFLOW | CCAR_OFLOW)) != 0)
		return (EINVAL);

	/*
	 * XXX xon/xoff not supported by the firmware!
	 * This is handled within FreeBSD only and may overflow buffers
	 * because of delayed reaction due to device buffering.
	 */

	return (0);
}

static __inline void
ubser_inc_tx_unit(struct ubser_softc *sc)
{
	sc->sc_curr_tx_unit++;
	if (sc->sc_curr_tx_unit >= sc->sc_numser) {
		sc->sc_curr_tx_unit = 0;
	}
}

static void
ubser_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ubser_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint8_t buf[1];
	uint8_t first_unit = sc->sc_curr_tx_unit;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:
tr_setup:
		pc = usbd_xfer_get_frame(xfer, 0);
		do {
			if (ucom_get_data(sc->sc_ucom + sc->sc_curr_tx_unit,
			    pc, 1, sc->sc_tx_size - 1,
			    &actlen)) {

				buf[0] = sc->sc_curr_tx_unit;

				usbd_copy_in(pc, 0, buf, 1);

				usbd_xfer_set_frame_len(xfer, 0, actlen + 1);
				usbd_transfer_submit(xfer);

				ubser_inc_tx_unit(sc);	/* round robin */

				break;
			}
			ubser_inc_tx_unit(sc);

		} while (sc->sc_curr_tx_unit != first_unit);

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
ubser_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ubser_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint8_t buf[1];
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		if (actlen < 1) {
			DPRINTF("invalid actlen=0!\n");
			goto tr_setup;
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, buf, 1);

		if (buf[0] >= sc->sc_numser) {
			DPRINTF("invalid serial number!\n");
			goto tr_setup;
		}
		ucom_put_data(sc->sc_ucom + buf[0], pc, 1, actlen - 1);

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
ubser_cfg_set_break(struct ucom_softc *ucom, uint8_t onoff)
{
	struct ubser_softc *sc = ucom->sc_parent;
	uint8_t x = ucom->sc_portno;
	struct usb_device_request req;
	usb_error_t err;

	if (onoff) {

		req.bmRequestType = UT_READ_VENDOR_INTERFACE;
		req.bRequest = VENDOR_SET_BREAK;
		req.wValue[0] = x;
		req.wValue[1] = 0;
		req.wIndex[0] = sc->sc_iface_no;
		req.wIndex[1] = 0;
		USETW(req.wLength, 0);

		err = ucom_cfg_do_request(sc->sc_udev, ucom, 
		    &req, NULL, 0, 1000);
		if (err) {
			DPRINTFN(0, "send break failed, error=%s\n",
			    usbd_errstr(err));
		}
	}
}

static void
ubser_cfg_get_status(struct ucom_softc *ucom, uint8_t *lsr, uint8_t *msr)
{
	/* fake status bits */
	*lsr = 0;
	*msr = SER_DCD;
}

static void
ubser_start_read(struct ucom_softc *ucom)
{
	struct ubser_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[UBSER_BULK_DT_RD]);
}

static void
ubser_stop_read(struct ucom_softc *ucom)
{
	struct ubser_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[UBSER_BULK_DT_RD]);
}

static void
ubser_start_write(struct ucom_softc *ucom)
{
	struct ubser_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[UBSER_BULK_DT_WR]);
}

static void
ubser_stop_write(struct ucom_softc *ucom)
{
	struct ubser_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[UBSER_BULK_DT_WR]);
}

static void
ubser_poll(struct ucom_softc *ucom)
{
	struct ubser_softc *sc = ucom->sc_parent;
	usbd_transfer_poll(sc->sc_xfer, UBSER_N_TRANSFER);
}
