#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Device driver for Cypress CY7C637xx and CY7C640/1xx series USB to
 * RS232 bridges.
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

#define	UCYCOM_MAX_IOLEN	(1024 + 2)	/* bytes */

#define	UCYCOM_IFACE_INDEX	0

enum {
	UCYCOM_CTRL_RD,
	UCYCOM_INTR_RD,
	UCYCOM_N_TRANSFER,
};

struct ucycom_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom;

	struct usb_device *sc_udev;
	struct usb_xfer *sc_xfer[UCYCOM_N_TRANSFER];
	struct mtx sc_mtx;

	uint32_t sc_model;
#define	MODEL_CY7C63743		0x63743
#define	MODEL_CY7C64013		0x64013

	uint16_t sc_flen;		/* feature report length */
	uint16_t sc_ilen;		/* input report length */
	uint16_t sc_olen;		/* output report length */

	uint8_t	sc_fid;			/* feature report id */
	uint8_t	sc_iid;			/* input report id */
	uint8_t	sc_oid;			/* output report id */
	uint8_t	sc_cfg;
#define	UCYCOM_CFG_RESET	0x80
#define	UCYCOM_CFG_PARODD	0x20
#define	UCYCOM_CFG_PAREN	0x10
#define	UCYCOM_CFG_STOPB	0x08
#define	UCYCOM_CFG_DATAB	0x03
	uint8_t	sc_ist;			/* status flags from last input */
	uint8_t	sc_iface_no;
	uint8_t	sc_temp_cfg[32];
};

/* prototypes */

static device_probe_t ucycom_probe;
static device_attach_t ucycom_attach;
static device_detach_t ucycom_detach;
static void ucycom_free_softc(struct ucycom_softc *);

static usb_callback_t ucycom_ctrl_write_callback;
static usb_callback_t ucycom_intr_read_callback;

static void	ucycom_free(struct ucom_softc *);
static void	ucycom_cfg_open(struct ucom_softc *);
static void	ucycom_start_read(struct ucom_softc *);
static void	ucycom_stop_read(struct ucom_softc *);
static void	ucycom_start_write(struct ucom_softc *);
static void	ucycom_stop_write(struct ucom_softc *);
static void	ucycom_cfg_write(struct ucycom_softc *, uint32_t, uint8_t);
static int	ucycom_pre_param(struct ucom_softc *, struct termios *);
static void	ucycom_cfg_param(struct ucom_softc *, struct termios *);
static void	ucycom_poll(struct ucom_softc *ucom);

static const struct usb_config ucycom_config[UCYCOM_N_TRANSFER] = {

	[UCYCOM_CTRL_RD] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = (sizeof(struct usb_device_request) + UCYCOM_MAX_IOLEN),
		.callback = &ucycom_ctrl_write_callback,
		.timeout = 1000,	/* 1 second */
	},

	[UCYCOM_INTR_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = UCYCOM_MAX_IOLEN,
		.callback = &ucycom_intr_read_callback,
	},
};

static const struct ucom_callback ucycom_callback = {
	.ucom_cfg_param = &ucycom_cfg_param,
	.ucom_cfg_open = &ucycom_cfg_open,
	.ucom_pre_param = &ucycom_pre_param,
	.ucom_start_read = &ucycom_start_read,
	.ucom_stop_read = &ucycom_stop_read,
	.ucom_start_write = &ucycom_start_write,
	.ucom_stop_write = &ucycom_stop_write,
	.ucom_poll = &ucycom_poll,
	.ucom_free = &ucycom_free,
};

static device_method_t ucycom_methods[] = {
	DEVMETHOD(device_probe, ucycom_probe),
	DEVMETHOD(device_attach, ucycom_attach),
	DEVMETHOD(device_detach, ucycom_detach),
	DEVMETHOD_END
};

static devclass_t ucycom_devclass;

static driver_t ucycom_driver = {
	.name = "ucycom",
	.methods = ucycom_methods,
	.size = sizeof(struct ucycom_softc),
};

/*
 * Supported devices
 */
static const STRUCT_USB_HOST_ID ucycom_devs[] = {
	{USB_VPI(USB_VENDOR_DELORME, USB_PRODUCT_DELORME_EARTHMATE, MODEL_CY7C64013)},
};

DRIVER_MODULE(ucycom, uhub, ucycom_driver, ucycom_devclass, NULL, 0);
MODULE_DEPEND(ucycom, ucom, 1, 1, 1);
MODULE_DEPEND(ucycom, usb, 1, 1, 1);
MODULE_VERSION(ucycom, 1);
USB_PNP_HOST_INFO(ucycom_devs);

#define	UCYCOM_DEFAULT_RATE	 4800
#define	UCYCOM_DEFAULT_CFG	 0x03	/* N-8-1 */

static int
ucycom_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST) {
		return (ENXIO);
	}
	if (uaa->info.bConfigIndex != 0) {
		return (ENXIO);
	}
	if (uaa->info.bIfaceIndex != UCYCOM_IFACE_INDEX) {
		return (ENXIO);
	}
	return (usbd_lookup_id_by_uaa(ucycom_devs, sizeof(ucycom_devs), uaa));
}

static int
ucycom_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct ucycom_softc *sc = device_get_softc(dev);
	void *urd_ptr = NULL;
	int32_t error;
	uint16_t urd_len;
	uint8_t iface_index;

	sc->sc_udev = uaa->device;

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "ucycom", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);

	DPRINTF("\n");

	/* get chip model */
	sc->sc_model = USB_GET_DRIVER_INFO(uaa);
	if (sc->sc_model == 0) {
		device_printf(dev, "unsupported device\n");
		goto detach;
	}
	device_printf(dev, "Cypress CY7C%X USB to RS232 bridge\n", sc->sc_model);

	/* get report descriptor */

	error = usbd_req_get_hid_desc(uaa->device, NULL,
	    &urd_ptr, &urd_len, M_USBDEV,
	    UCYCOM_IFACE_INDEX);

	if (error) {
		device_printf(dev, "failed to get report "
		    "descriptor: %s\n",
		    usbd_errstr(error));
		goto detach;
	}
	/* get report sizes */

	sc->sc_flen = hid_report_size(urd_ptr, urd_len, hid_feature, &sc->sc_fid);
	sc->sc_ilen = hid_report_size(urd_ptr, urd_len, hid_input, &sc->sc_iid);
	sc->sc_olen = hid_report_size(urd_ptr, urd_len, hid_output, &sc->sc_oid);

	if ((sc->sc_ilen > UCYCOM_MAX_IOLEN) || (sc->sc_ilen < 1) ||
	    (sc->sc_olen > UCYCOM_MAX_IOLEN) || (sc->sc_olen < 2) ||
	    (sc->sc_flen > UCYCOM_MAX_IOLEN) || (sc->sc_flen < 5)) {
		device_printf(dev, "invalid report size i=%d, o=%d, f=%d, max=%d\n",
		    sc->sc_ilen, sc->sc_olen, sc->sc_flen,
		    UCYCOM_MAX_IOLEN);
		goto detach;
	}
	sc->sc_iface_no = uaa->info.bIfaceNum;

	iface_index = UCYCOM_IFACE_INDEX;
	error = usbd_transfer_setup(uaa->device, &iface_index,
	    sc->sc_xfer, ucycom_config, UCYCOM_N_TRANSFER,
	    sc, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "allocating USB "
		    "transfers failed\n");
		goto detach;
	}
	error = ucom_attach(&sc->sc_super_ucom, &sc->sc_ucom, 1, sc,
	    &ucycom_callback, &sc->sc_mtx);
	if (error) {
		goto detach;
	}
	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);

	if (urd_ptr) {
		free(urd_ptr, M_USBDEV);
	}

	return (0);			/* success */

detach:
	if (urd_ptr) {
		free(urd_ptr, M_USBDEV);
	}
	ucycom_detach(dev);
	return (ENXIO);
}

static int
ucycom_detach(device_t dev)
{
	struct ucycom_softc *sc = device_get_softc(dev);

	ucom_detach(&sc->sc_super_ucom, &sc->sc_ucom);
	usbd_transfer_unsetup(sc->sc_xfer, UCYCOM_N_TRANSFER);

	device_claim_softc(dev);

	ucycom_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(ucycom);

static void
ucycom_free_softc(struct ucycom_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
ucycom_free(struct ucom_softc *ucom)
{
	ucycom_free_softc(ucom->sc_parent);
}

static void
ucycom_cfg_open(struct ucom_softc *ucom)
{
	struct ucycom_softc *sc = ucom->sc_parent;

	/* set default configuration */
	ucycom_cfg_write(sc, UCYCOM_DEFAULT_RATE, UCYCOM_DEFAULT_CFG);
}

static void
ucycom_start_read(struct ucom_softc *ucom)
{
	struct ucycom_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[UCYCOM_INTR_RD]);
}

static void
ucycom_stop_read(struct ucom_softc *ucom)
{
	struct ucycom_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[UCYCOM_INTR_RD]);
}

static void
ucycom_start_write(struct ucom_softc *ucom)
{
	struct ucycom_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[UCYCOM_CTRL_RD]);
}

static void
ucycom_stop_write(struct ucom_softc *ucom)
{
	struct ucycom_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[UCYCOM_CTRL_RD]);
}

static void
ucycom_ctrl_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ucycom_softc *sc = usbd_xfer_softc(xfer);
	struct usb_device_request req;
	struct usb_page_cache *pc0, *pc1;
	uint8_t data[2];
	uint8_t offset;
	uint32_t actlen;

	pc0 = usbd_xfer_get_frame(xfer, 0);
	pc1 = usbd_xfer_get_frame(xfer, 1);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
tr_transferred:
	case USB_ST_SETUP:

		switch (sc->sc_model) {
		case MODEL_CY7C63743:
			offset = 1;
			break;
		case MODEL_CY7C64013:
			offset = 2;
			break;
		default:
			offset = 0;
			break;
		}

		if (ucom_get_data(&sc->sc_ucom, pc1, offset,
		    sc->sc_olen - offset, &actlen)) {

			req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
			req.bRequest = UR_SET_REPORT;
			USETW2(req.wValue, UHID_OUTPUT_REPORT, sc->sc_oid);
			req.wIndex[0] = sc->sc_iface_no;
			req.wIndex[1] = 0;
			USETW(req.wLength, sc->sc_olen);

			switch (sc->sc_model) {
			case MODEL_CY7C63743:
				data[0] = actlen;
				break;
			case MODEL_CY7C64013:
				data[0] = 0;
				data[1] = actlen;
				break;
			default:
				break;
			}

			usbd_copy_in(pc0, 0, &req, sizeof(req));
			usbd_copy_in(pc1, 0, data, offset);

			usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
			usbd_xfer_set_frame_len(xfer, 1, sc->sc_olen);
			usbd_xfer_set_frames(xfer, sc->sc_olen ? 2 : 1);
			usbd_transfer_submit(xfer);
		}
		return;

	default:			/* Error */
		if (error == USB_ERR_CANCELLED) {
			return;
		}
		DPRINTF("error=%s\n",
		    usbd_errstr(error));
		goto tr_transferred;
	}
}

static void
ucycom_cfg_write(struct ucycom_softc *sc, uint32_t baud, uint8_t cfg)
{
	struct usb_device_request req;
	uint16_t len;
	usb_error_t err;

	len = sc->sc_flen;
	if (len > sizeof(sc->sc_temp_cfg)) {
		len = sizeof(sc->sc_temp_cfg);
	}
	sc->sc_cfg = cfg;

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UR_SET_REPORT;
	USETW2(req.wValue, UHID_FEATURE_REPORT, sc->sc_fid);
	req.wIndex[0] = sc->sc_iface_no;
	req.wIndex[1] = 0;
	USETW(req.wLength, len);

	sc->sc_temp_cfg[0] = (baud & 0xff);
	sc->sc_temp_cfg[1] = (baud >> 8) & 0xff;
	sc->sc_temp_cfg[2] = (baud >> 16) & 0xff;
	sc->sc_temp_cfg[3] = (baud >> 24) & 0xff;
	sc->sc_temp_cfg[4] = cfg;

	err = ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom,
	    &req, sc->sc_temp_cfg, 0, 1000);
	if (err) {
		DPRINTFN(0, "device request failed, err=%s "
		    "(ignored)\n", usbd_errstr(err));
	}
}

static int
ucycom_pre_param(struct ucom_softc *ucom, struct termios *t)
{
	switch (t->c_ospeed) {
		case 600:
		case 1200:
		case 2400:
		case 4800:
		case 9600:
		case 19200:
		case 38400:
		case 57600:
#if 0
		/*
		 * Stock chips only support standard baud rates in the 600 - 57600
		 * range, but higher rates can be achieved using custom firmware.
		 */
		case 115200:
		case 153600:
		case 192000:
#endif
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

static void
ucycom_cfg_param(struct ucom_softc *ucom, struct termios *t)
{
	struct ucycom_softc *sc = ucom->sc_parent;
	uint8_t cfg;

	DPRINTF("\n");

	if (t->c_cflag & CIGNORE) {
		cfg = sc->sc_cfg;
	} else {
		cfg = 0;
		switch (t->c_cflag & CSIZE) {
		default:
		case CS8:
			++cfg;
		case CS7:
			++cfg;
		case CS6:
			++cfg;
		case CS5:
			break;
		}

		if (t->c_cflag & CSTOPB)
			cfg |= UCYCOM_CFG_STOPB;
		if (t->c_cflag & PARENB)
			cfg |= UCYCOM_CFG_PAREN;
		if (t->c_cflag & PARODD)
			cfg |= UCYCOM_CFG_PARODD;
	}

	ucycom_cfg_write(sc, t->c_ospeed, cfg);
}

static void
ucycom_intr_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ucycom_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint8_t buf[2];
	uint32_t offset;
	int len;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);
	pc = usbd_xfer_get_frame(xfer, 0);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		switch (sc->sc_model) {
		case MODEL_CY7C63743:
			if (actlen < 1) {
				goto tr_setup;
			}
			usbd_copy_out(pc, 0, buf, 1);

			sc->sc_ist = buf[0] & ~0x07;
			len = buf[0] & 0x07;

			actlen--;
			offset = 1;

			break;

		case MODEL_CY7C64013:
			if (actlen < 2) {
				goto tr_setup;
			}
			usbd_copy_out(pc, 0, buf, 2);

			sc->sc_ist = buf[0] & ~0x07;
			len = buf[1];

			actlen -= 2;
			offset = 2;

			break;

		default:
			DPRINTFN(0, "unsupported model number\n");
			goto tr_setup;
		}

		if (len > actlen)
			len = actlen;
		if (len)
			ucom_put_data(&sc->sc_ucom, pc, offset, len);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, sc->sc_ilen);
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
ucycom_poll(struct ucom_softc *ucom)
{
	struct ucycom_softc *sc = ucom->sc_parent;
	usbd_transfer_poll(sc->sc_xfer, UCYCOM_N_TRANSFER);
}
