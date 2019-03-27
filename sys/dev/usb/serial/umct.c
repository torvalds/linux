#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Scott Long
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
 */

/*
 * Driver for the MCT (Magic Control Technology) USB-RS232 Converter.
 * Based on the superb documentation from the linux mct_u232 driver by
 * Wolfgang Grandeggar <wolfgang@cec.ch>.
 * This device smells a lot like the Belkin F5U103, except that it has
 * suffered some mild brain-damage.  This driver is based off of the ubsa.c
 * driver from Alexander Kabaev <kan@FreeBSD.org>.  Merging the two together
 * might be useful, though the subtle differences might lead to lots of
 * #ifdef's.
 */

/*
 * NOTE: all function names beginning like "umct_cfg_" can only
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
#include "usbdevs.h"

#define	USB_DEBUG_VAR usb_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/serial/usb_serial.h>

/* The UMCT advertises the standard 8250 UART registers */
#define	UMCT_GET_MSR		2	/* Get Modem Status Register */
#define	UMCT_GET_MSR_SIZE	1
#define	UMCT_GET_LCR		6	/* Get Line Control Register */
#define	UMCT_GET_LCR_SIZE	1
#define	UMCT_SET_BAUD		5	/* Set the Baud Rate Divisor */
#define	UMCT_SET_BAUD_SIZE	4
#define	UMCT_SET_LCR		7	/* Set Line Control Register */
#define	UMCT_SET_LCR_SIZE	1
#define	UMCT_SET_MCR		10	/* Set Modem Control Register */
#define	UMCT_SET_MCR_SIZE	1

#define	UMCT_MSR_CTS_CHG	0x01
#define	UMCT_MSR_DSR_CHG	0x02
#define	UMCT_MSR_RI_CHG		0x04
#define	UMCT_MSR_CD_CHG		0x08
#define	UMCT_MSR_CTS		0x10
#define	UMCT_MSR_RTS		0x20
#define	UMCT_MSR_RI		0x40
#define	UMCT_MSR_CD		0x80

#define	UMCT_INTR_INTERVAL	100
#define	UMCT_IFACE_INDEX	0
#define	UMCT_CONFIG_INDEX	0

enum {
	UMCT_BULK_DT_WR,
	UMCT_BULK_DT_RD,
	UMCT_INTR_DT_RD,
	UMCT_N_TRANSFER,
};

struct umct_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom;

	struct usb_device *sc_udev;
	struct usb_xfer *sc_xfer[UMCT_N_TRANSFER];
	struct mtx sc_mtx;

	uint32_t sc_unit;

	uint16_t sc_obufsize;

	uint8_t	sc_lsr;
	uint8_t	sc_msr;
	uint8_t	sc_lcr;
	uint8_t	sc_mcr;
	uint8_t	sc_iface_no;
	uint8_t sc_swap_cb;
};

/* prototypes */

static device_probe_t umct_probe;
static device_attach_t umct_attach;
static device_detach_t umct_detach;
static void umct_free_softc(struct umct_softc *);

static usb_callback_t umct_intr_callback;
static usb_callback_t umct_intr_callback_sub;
static usb_callback_t umct_read_callback;
static usb_callback_t umct_read_callback_sub;
static usb_callback_t umct_write_callback;

static void	umct_cfg_do_request(struct umct_softc *sc, uint8_t request,
		    uint16_t len, uint32_t value);
static void	umct_free(struct ucom_softc *);
static void	umct_cfg_get_status(struct ucom_softc *, uint8_t *,
		    uint8_t *);
static void	umct_cfg_set_break(struct ucom_softc *, uint8_t);
static void	umct_cfg_set_dtr(struct ucom_softc *, uint8_t);
static void	umct_cfg_set_rts(struct ucom_softc *, uint8_t);
static uint8_t	umct_calc_baud(uint32_t);
static int	umct_pre_param(struct ucom_softc *, struct termios *);
static void	umct_cfg_param(struct ucom_softc *, struct termios *);
static void	umct_start_read(struct ucom_softc *);
static void	umct_stop_read(struct ucom_softc *);
static void	umct_start_write(struct ucom_softc *);
static void	umct_stop_write(struct ucom_softc *);
static void	umct_poll(struct ucom_softc *ucom);

static const struct usb_config umct_config[UMCT_N_TRANSFER] = {

	[UMCT_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = 0,	/* use wMaxPacketSize */
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &umct_write_callback,
	},

	[UMCT_BULK_DT_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,	/* use wMaxPacketSize */
		.callback = &umct_read_callback,
		.ep_index = 0,		/* first interrupt endpoint */
	},

	[UMCT_INTR_DT_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,	/* use wMaxPacketSize */
		.callback = &umct_intr_callback,
		.ep_index = 1,		/* second interrupt endpoint */
	},
};

static const struct ucom_callback umct_callback = {
	.ucom_cfg_get_status = &umct_cfg_get_status,
	.ucom_cfg_set_dtr = &umct_cfg_set_dtr,
	.ucom_cfg_set_rts = &umct_cfg_set_rts,
	.ucom_cfg_set_break = &umct_cfg_set_break,
	.ucom_cfg_param = &umct_cfg_param,
	.ucom_pre_param = &umct_pre_param,
	.ucom_start_read = &umct_start_read,
	.ucom_stop_read = &umct_stop_read,
	.ucom_start_write = &umct_start_write,
	.ucom_stop_write = &umct_stop_write,
	.ucom_poll = &umct_poll,
	.ucom_free = &umct_free,
};

static const STRUCT_USB_HOST_ID umct_devs[] = {
	{USB_VPI(USB_VENDOR_MCT, USB_PRODUCT_MCT_USB232, 0)},
	{USB_VPI(USB_VENDOR_MCT, USB_PRODUCT_MCT_SITECOM_USB232, 0)},
	{USB_VPI(USB_VENDOR_MCT, USB_PRODUCT_MCT_DU_H3SP_USB232, 0)},
	{USB_VPI(USB_VENDOR_BELKIN, USB_PRODUCT_BELKIN_F5U109, 0)},
	{USB_VPI(USB_VENDOR_BELKIN, USB_PRODUCT_BELKIN_F5U409, 0)},
};

static device_method_t umct_methods[] = {
	DEVMETHOD(device_probe, umct_probe),
	DEVMETHOD(device_attach, umct_attach),
	DEVMETHOD(device_detach, umct_detach),
	DEVMETHOD_END
};

static devclass_t umct_devclass;

static driver_t umct_driver = {
	.name = "umct",
	.methods = umct_methods,
	.size = sizeof(struct umct_softc),
};

DRIVER_MODULE(umct, uhub, umct_driver, umct_devclass, NULL, 0);
MODULE_DEPEND(umct, ucom, 1, 1, 1);
MODULE_DEPEND(umct, usb, 1, 1, 1);
MODULE_VERSION(umct, 1);
USB_PNP_HOST_INFO(umct_devs);

static int
umct_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST) {
		return (ENXIO);
	}
	if (uaa->info.bConfigIndex != UMCT_CONFIG_INDEX) {
		return (ENXIO);
	}
	if (uaa->info.bIfaceIndex != UMCT_IFACE_INDEX) {
		return (ENXIO);
	}
	return (usbd_lookup_id_by_uaa(umct_devs, sizeof(umct_devs), uaa));
}

static int
umct_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct umct_softc *sc = device_get_softc(dev);
	int32_t error;
	uint16_t maxp;
	uint8_t iface_index;

	sc->sc_udev = uaa->device;
	sc->sc_unit = device_get_unit(dev);

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "umct", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);

	sc->sc_iface_no = uaa->info.bIfaceNum;

	iface_index = UMCT_IFACE_INDEX;
	error = usbd_transfer_setup(uaa->device, &iface_index,
	    sc->sc_xfer, umct_config, UMCT_N_TRANSFER, sc, &sc->sc_mtx);

	if (error) {
		device_printf(dev, "allocating USB "
		    "transfers failed\n");
		goto detach;
	}

	/*
	 * The real bulk-in endpoint is also marked as an interrupt.
	 * The only way to differentiate it from the real interrupt
	 * endpoint is to look at the wMaxPacketSize field.
	 */
	maxp = usbd_xfer_max_framelen(sc->sc_xfer[UMCT_BULK_DT_RD]);
	if (maxp == 0x2) {

		/* guessed wrong - switch around endpoints */

		struct usb_xfer *temp = sc->sc_xfer[UMCT_INTR_DT_RD];

		sc->sc_xfer[UMCT_INTR_DT_RD] = sc->sc_xfer[UMCT_BULK_DT_RD];
		sc->sc_xfer[UMCT_BULK_DT_RD] = temp;
		sc->sc_swap_cb = 1;
	}

	sc->sc_obufsize = usbd_xfer_max_len(sc->sc_xfer[UMCT_BULK_DT_WR]);

	if (uaa->info.idProduct == USB_PRODUCT_MCT_SITECOM_USB232) {
		if (sc->sc_obufsize > 16) {
			sc->sc_obufsize = 16;
		}
	}
	error = ucom_attach(&sc->sc_super_ucom, &sc->sc_ucom, 1, sc,
	    &umct_callback, &sc->sc_mtx);
	if (error) {
		goto detach;
	}
	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);

	return (0);			/* success */

detach:
	umct_detach(dev);
	return (ENXIO);			/* failure */
}

static int
umct_detach(device_t dev)
{
	struct umct_softc *sc = device_get_softc(dev);

	ucom_detach(&sc->sc_super_ucom, &sc->sc_ucom);
	usbd_transfer_unsetup(sc->sc_xfer, UMCT_N_TRANSFER);

	device_claim_softc(dev);

	umct_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(umct);

static void
umct_free_softc(struct umct_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
umct_free(struct ucom_softc *ucom)
{
	umct_free_softc(ucom->sc_parent);
}

static void
umct_cfg_do_request(struct umct_softc *sc, uint8_t request,
    uint16_t len, uint32_t value)
{
	struct usb_device_request req;
	usb_error_t err;
	uint8_t temp[4];

	if (len > 4)
		len = 4;
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = request;
	USETW(req.wValue, 0);
	req.wIndex[0] = sc->sc_iface_no;
	req.wIndex[1] = 0;
	USETW(req.wLength, len);
	USETDW(temp, value);

	err = ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, temp, 0, 1000);
	if (err) {
		DPRINTFN(0, "device request failed, err=%s "
		    "(ignored)\n", usbd_errstr(err));
	}
	return;
}

static void
umct_intr_callback_sub(struct usb_xfer *xfer, usb_error_t error)
{
	struct umct_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint8_t buf[2];
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		if (actlen < 2) {
			DPRINTF("too short message\n");
			goto tr_setup;
		}
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, buf, sizeof(buf));

		/*
		 * MSR bits need translation from ns16550 to SER_* values.
		 * LSR bits are ns16550 in hardware and ucom.
		 */
		sc->sc_msr = 0;
		if (buf[0] & UMCT_MSR_CTS)
			sc->sc_msr |= SER_CTS;
		if (buf[0] & UMCT_MSR_CD)
			sc->sc_msr |= SER_DCD;
		if (buf[0] & UMCT_MSR_RI)
			sc->sc_msr |= SER_RI;
		if (buf[0] & UMCT_MSR_RTS)
			sc->sc_msr |= SER_DSR;
		sc->sc_lsr = buf[1];

		ucom_status_change(&sc->sc_ucom);
		/* FALLTHROUGH */
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
umct_cfg_get_status(struct ucom_softc *ucom, uint8_t *lsr, uint8_t *msr)
{
	struct umct_softc *sc = ucom->sc_parent;

	*lsr = sc->sc_lsr;
	*msr = sc->sc_msr;
}

static void
umct_cfg_set_break(struct ucom_softc *ucom, uint8_t onoff)
{
	struct umct_softc *sc = ucom->sc_parent;

	if (onoff)
		sc->sc_lcr |= 0x40;
	else
		sc->sc_lcr &= ~0x40;

	umct_cfg_do_request(sc, UMCT_SET_LCR, UMCT_SET_LCR_SIZE, sc->sc_lcr);
}

static void
umct_cfg_set_dtr(struct ucom_softc *ucom, uint8_t onoff)
{
	struct umct_softc *sc = ucom->sc_parent;

	if (onoff)
		sc->sc_mcr |= 0x01;
	else
		sc->sc_mcr &= ~0x01;

	umct_cfg_do_request(sc, UMCT_SET_MCR, UMCT_SET_MCR_SIZE, sc->sc_mcr);
}

static void
umct_cfg_set_rts(struct ucom_softc *ucom, uint8_t onoff)
{
	struct umct_softc *sc = ucom->sc_parent;

	if (onoff)
		sc->sc_mcr |= 0x02;
	else
		sc->sc_mcr &= ~0x02;

	umct_cfg_do_request(sc, UMCT_SET_MCR, UMCT_SET_MCR_SIZE, sc->sc_mcr);
}

static uint8_t
umct_calc_baud(uint32_t baud)
{
	switch (baud) {
		case B300:return (0x1);
	case B600:
		return (0x2);
	case B1200:
		return (0x3);
	case B2400:
		return (0x4);
	case B4800:
		return (0x6);
	case B9600:
		return (0x8);
	case B19200:
		return (0x9);
	case B38400:
		return (0xa);
	case B57600:
		return (0xb);
	case 115200:
		return (0xc);
	case B0:
	default:
		break;
	}
	return (0x0);
}

static int
umct_pre_param(struct ucom_softc *ucom, struct termios *t)
{
	return (0);			/* we accept anything */
}

static void
umct_cfg_param(struct ucom_softc *ucom, struct termios *t)
{
	struct umct_softc *sc = ucom->sc_parent;
	uint32_t value;

	value = umct_calc_baud(t->c_ospeed);
	umct_cfg_do_request(sc, UMCT_SET_BAUD, UMCT_SET_BAUD_SIZE, value);

	value = (sc->sc_lcr & 0x40);

	switch (t->c_cflag & CSIZE) {
	case CS5:
		value |= 0x0;
		break;
	case CS6:
		value |= 0x1;
		break;
	case CS7:
		value |= 0x2;
		break;
	default:
	case CS8:
		value |= 0x3;
		break;
	}

	value |= (t->c_cflag & CSTOPB) ? 0x4 : 0;
	if (t->c_cflag & PARENB) {
		value |= 0x8;
		value |= (t->c_cflag & PARODD) ? 0x0 : 0x10;
	}
	/*
	 * XXX There doesn't seem to be a way to tell the device
	 * to use flow control.
	 */

	sc->sc_lcr = value;
	umct_cfg_do_request(sc, UMCT_SET_LCR, UMCT_SET_LCR_SIZE, value);
}

static void
umct_start_read(struct ucom_softc *ucom)
{
	struct umct_softc *sc = ucom->sc_parent;

	/* start interrupt endpoint */
	usbd_transfer_start(sc->sc_xfer[UMCT_INTR_DT_RD]);

	/* start read endpoint */
	usbd_transfer_start(sc->sc_xfer[UMCT_BULK_DT_RD]);
}

static void
umct_stop_read(struct ucom_softc *ucom)
{
	struct umct_softc *sc = ucom->sc_parent;

	/* stop interrupt endpoint */
	usbd_transfer_stop(sc->sc_xfer[UMCT_INTR_DT_RD]);

	/* stop read endpoint */
	usbd_transfer_stop(sc->sc_xfer[UMCT_BULK_DT_RD]);
}

static void
umct_start_write(struct ucom_softc *ucom)
{
	struct umct_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[UMCT_BULK_DT_WR]);
}

static void
umct_stop_write(struct ucom_softc *ucom)
{
	struct umct_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[UMCT_BULK_DT_WR]);
}

static void
umct_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umct_softc *sc = usbd_xfer_softc(xfer);

	if (sc->sc_swap_cb)
		umct_intr_callback_sub(xfer, error);
	else
		umct_read_callback_sub(xfer, error);
}

static void
umct_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umct_softc *sc = usbd_xfer_softc(xfer);

	if (sc->sc_swap_cb)
		umct_read_callback_sub(xfer, error);
	else
		umct_intr_callback_sub(xfer, error);
}

static void
umct_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umct_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:
tr_setup:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (ucom_get_data(&sc->sc_ucom, pc, 0,
		    sc->sc_obufsize, &actlen)) {

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
umct_read_callback_sub(struct usb_xfer *xfer, usb_error_t error)
{
	struct umct_softc *sc = usbd_xfer_softc(xfer);
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
umct_poll(struct ucom_softc *ucom)
{
	struct umct_softc *sc = ucom->sc_parent;
	usbd_transfer_poll(sc->sc_xfer, UMCT_N_TRANSFER);
}
