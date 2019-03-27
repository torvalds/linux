/*	$NetBSD: usb/uvscom.c,v 1.1 2002/03/19 15:08:42 augustss Exp $	*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2003, 2005 Shunsuke Akiyama <akiyama@jp.FreeBSD.org>.
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
 * uvscom: SUNTAC Slipper U VS-10U driver.
 * Slipper U is a PC Card to USB converter for data communication card
 * adapter.  It supports DDI Pocket's Air H" C@rd, C@rd H" 64, NTT's P-in,
 * P-in m@ater and various data communication card adapters.
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

#define	USB_DEBUG_VAR uvscom_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/serial/usb_serial.h>

#ifdef USB_DEBUG
static int uvscom_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, uvscom, CTLFLAG_RW, 0, "USB uvscom");
SYSCTL_INT(_hw_usb_uvscom, OID_AUTO, debug, CTLFLAG_RWTUN,
    &uvscom_debug, 0, "Debug level");
#endif

#define	UVSCOM_MODVER		1	/* module version */

#define	UVSCOM_CONFIG_INDEX	0
#define	UVSCOM_IFACE_INDEX	0

/* Request */
#define	UVSCOM_SET_SPEED	0x10
#define	UVSCOM_LINE_CTL		0x11
#define	UVSCOM_SET_PARAM	0x12
#define	UVSCOM_READ_STATUS	0xd0
#define	UVSCOM_SHUTDOWN		0xe0

/* UVSCOM_SET_SPEED parameters */
#define	UVSCOM_SPEED_150BPS	0x00
#define	UVSCOM_SPEED_300BPS	0x01
#define	UVSCOM_SPEED_600BPS	0x02
#define	UVSCOM_SPEED_1200BPS	0x03
#define	UVSCOM_SPEED_2400BPS	0x04
#define	UVSCOM_SPEED_4800BPS	0x05
#define	UVSCOM_SPEED_9600BPS	0x06
#define	UVSCOM_SPEED_19200BPS	0x07
#define	UVSCOM_SPEED_38400BPS	0x08
#define	UVSCOM_SPEED_57600BPS	0x09
#define	UVSCOM_SPEED_115200BPS	0x0a

/* UVSCOM_LINE_CTL parameters */
#define	UVSCOM_BREAK		0x40
#define	UVSCOM_RTS		0x02
#define	UVSCOM_DTR		0x01
#define	UVSCOM_LINE_INIT	0x08

/* UVSCOM_SET_PARAM parameters */
#define	UVSCOM_DATA_MASK	0x03
#define	UVSCOM_DATA_BIT_8	0x03
#define	UVSCOM_DATA_BIT_7	0x02
#define	UVSCOM_DATA_BIT_6	0x01
#define	UVSCOM_DATA_BIT_5	0x00

#define	UVSCOM_STOP_MASK	0x04
#define	UVSCOM_STOP_BIT_2	0x04
#define	UVSCOM_STOP_BIT_1	0x00

#define	UVSCOM_PARITY_MASK	0x18
#define	UVSCOM_PARITY_EVEN	0x18
#define	UVSCOM_PARITY_ODD	0x08
#define	UVSCOM_PARITY_NONE	0x00

/* Status bits */
#define	UVSCOM_TXRDY		0x04
#define	UVSCOM_RXRDY		0x01

#define	UVSCOM_DCD		0x08
#define	UVSCOM_NOCARD		0x04
#define	UVSCOM_DSR		0x02
#define	UVSCOM_CTS		0x01
#define	UVSCOM_USTAT_MASK	(UVSCOM_NOCARD | UVSCOM_DSR | UVSCOM_CTS)

#define	UVSCOM_BULK_BUF_SIZE	1024	/* bytes */

enum {
	UVSCOM_BULK_DT_WR,
	UVSCOM_BULK_DT_RD,
	UVSCOM_INTR_DT_RD,
	UVSCOM_N_TRANSFER,
};

struct uvscom_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom;

	struct usb_xfer *sc_xfer[UVSCOM_N_TRANSFER];
	struct usb_device *sc_udev;
	struct mtx sc_mtx;

	uint16_t sc_line;		/* line control register */

	uint8_t	sc_iface_no;		/* interface number */
	uint8_t	sc_iface_index;		/* interface index */
	uint8_t	sc_lsr;			/* local status register */
	uint8_t	sc_msr;			/* uvscom status register */
	uint8_t	sc_unit_status;		/* unit status */
};

/* prototypes */

static device_probe_t uvscom_probe;
static device_attach_t uvscom_attach;
static device_detach_t uvscom_detach;
static void uvscom_free_softc(struct uvscom_softc *);

static usb_callback_t uvscom_write_callback;
static usb_callback_t uvscom_read_callback;
static usb_callback_t uvscom_intr_callback;

static void	uvscom_free(struct ucom_softc *);
static void	uvscom_cfg_set_dtr(struct ucom_softc *, uint8_t);
static void	uvscom_cfg_set_rts(struct ucom_softc *, uint8_t);
static void	uvscom_cfg_set_break(struct ucom_softc *, uint8_t);
static int	uvscom_pre_param(struct ucom_softc *, struct termios *);
static void	uvscom_cfg_param(struct ucom_softc *, struct termios *);
static int	uvscom_pre_open(struct ucom_softc *);
static void	uvscom_cfg_open(struct ucom_softc *);
static void	uvscom_cfg_close(struct ucom_softc *);
static void	uvscom_start_read(struct ucom_softc *);
static void	uvscom_stop_read(struct ucom_softc *);
static void	uvscom_start_write(struct ucom_softc *);
static void	uvscom_stop_write(struct ucom_softc *);
static void	uvscom_cfg_get_status(struct ucom_softc *, uint8_t *,
		    uint8_t *);
static void	uvscom_cfg_write(struct uvscom_softc *, uint8_t, uint16_t);
static uint16_t	uvscom_cfg_read_status(struct uvscom_softc *);
static void	uvscom_poll(struct ucom_softc *ucom);

static const struct usb_config uvscom_config[UVSCOM_N_TRANSFER] = {

	[UVSCOM_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = UVSCOM_BULK_BUF_SIZE,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &uvscom_write_callback,
	},

	[UVSCOM_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = UVSCOM_BULK_BUF_SIZE,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &uvscom_read_callback,
	},

	[UVSCOM_INTR_DT_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,	/* use wMaxPacketSize */
		.callback = &uvscom_intr_callback,
	},
};

static const struct ucom_callback uvscom_callback = {
	.ucom_cfg_get_status = &uvscom_cfg_get_status,
	.ucom_cfg_set_dtr = &uvscom_cfg_set_dtr,
	.ucom_cfg_set_rts = &uvscom_cfg_set_rts,
	.ucom_cfg_set_break = &uvscom_cfg_set_break,
	.ucom_cfg_param = &uvscom_cfg_param,
	.ucom_cfg_open = &uvscom_cfg_open,
	.ucom_cfg_close = &uvscom_cfg_close,
	.ucom_pre_open = &uvscom_pre_open,
	.ucom_pre_param = &uvscom_pre_param,
	.ucom_start_read = &uvscom_start_read,
	.ucom_stop_read = &uvscom_stop_read,
	.ucom_start_write = &uvscom_start_write,
	.ucom_stop_write = &uvscom_stop_write,
	.ucom_poll = &uvscom_poll,
	.ucom_free = &uvscom_free,
};

static const STRUCT_USB_HOST_ID uvscom_devs[] = {
	/* SUNTAC U-Cable type A4 */
	{USB_VPI(USB_VENDOR_SUNTAC, USB_PRODUCT_SUNTAC_AS144L4, 0)},
	/* SUNTAC U-Cable type D2 */
	{USB_VPI(USB_VENDOR_SUNTAC, USB_PRODUCT_SUNTAC_DS96L, 0)},
	/* SUNTAC Ir-Trinity */
	{USB_VPI(USB_VENDOR_SUNTAC, USB_PRODUCT_SUNTAC_IS96U, 0)},
	/* SUNTAC U-Cable type P1 */
	{USB_VPI(USB_VENDOR_SUNTAC, USB_PRODUCT_SUNTAC_PS64P1, 0)},
	/* SUNTAC Slipper U */
	{USB_VPI(USB_VENDOR_SUNTAC, USB_PRODUCT_SUNTAC_VS10U, 0)},
};

static device_method_t uvscom_methods[] = {
	DEVMETHOD(device_probe, uvscom_probe),
	DEVMETHOD(device_attach, uvscom_attach),
	DEVMETHOD(device_detach, uvscom_detach),
	DEVMETHOD_END
};

static devclass_t uvscom_devclass;

static driver_t uvscom_driver = {
	.name = "uvscom",
	.methods = uvscom_methods,
	.size = sizeof(struct uvscom_softc),
};

DRIVER_MODULE(uvscom, uhub, uvscom_driver, uvscom_devclass, NULL, 0);
MODULE_DEPEND(uvscom, ucom, 1, 1, 1);
MODULE_DEPEND(uvscom, usb, 1, 1, 1);
MODULE_VERSION(uvscom, UVSCOM_MODVER);
USB_PNP_HOST_INFO(uvscom_devs);

static int
uvscom_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST) {
		return (ENXIO);
	}
	if (uaa->info.bConfigIndex != UVSCOM_CONFIG_INDEX) {
		return (ENXIO);
	}
	if (uaa->info.bIfaceIndex != UVSCOM_IFACE_INDEX) {
		return (ENXIO);
	}
	return (usbd_lookup_id_by_uaa(uvscom_devs, sizeof(uvscom_devs), uaa));
}

static int
uvscom_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct uvscom_softc *sc = device_get_softc(dev);
	int error;

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "uvscom", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);

	sc->sc_udev = uaa->device;

	DPRINTF("sc=%p\n", sc);

	sc->sc_iface_no = uaa->info.bIfaceNum;
	sc->sc_iface_index = UVSCOM_IFACE_INDEX;

	error = usbd_transfer_setup(uaa->device, &sc->sc_iface_index,
	    sc->sc_xfer, uvscom_config, UVSCOM_N_TRANSFER, sc, &sc->sc_mtx);

	if (error) {
		DPRINTF("could not allocate all USB transfers!\n");
		goto detach;
	}
	sc->sc_line = UVSCOM_LINE_INIT;

	/* clear stall at first run */
	mtx_lock(&sc->sc_mtx);
	usbd_xfer_set_stall(sc->sc_xfer[UVSCOM_BULK_DT_WR]);
	usbd_xfer_set_stall(sc->sc_xfer[UVSCOM_BULK_DT_RD]);
	mtx_unlock(&sc->sc_mtx);

	error = ucom_attach(&sc->sc_super_ucom, &sc->sc_ucom, 1, sc,
	    &uvscom_callback, &sc->sc_mtx);
	if (error) {
		goto detach;
	}
	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);

	/* start interrupt pipe */
	mtx_lock(&sc->sc_mtx);
	usbd_transfer_start(sc->sc_xfer[UVSCOM_INTR_DT_RD]);
	mtx_unlock(&sc->sc_mtx);

	return (0);

detach:
	uvscom_detach(dev);
	return (ENXIO);
}

static int
uvscom_detach(device_t dev)
{
	struct uvscom_softc *sc = device_get_softc(dev);

	DPRINTF("sc=%p\n", sc);

	/* stop interrupt pipe */

	if (sc->sc_xfer[UVSCOM_INTR_DT_RD])
		usbd_transfer_stop(sc->sc_xfer[UVSCOM_INTR_DT_RD]);

	ucom_detach(&sc->sc_super_ucom, &sc->sc_ucom);
	usbd_transfer_unsetup(sc->sc_xfer, UVSCOM_N_TRANSFER);

	device_claim_softc(dev);

	uvscom_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(uvscom);

static void
uvscom_free_softc(struct uvscom_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
uvscom_free(struct ucom_softc *ucom)
{
	uvscom_free_softc(ucom->sc_parent);
}

static void
uvscom_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uvscom_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:
tr_setup:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (ucom_get_data(&sc->sc_ucom, pc, 0,
		    UVSCOM_BULK_BUF_SIZE, &actlen)) {

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
uvscom_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uvscom_softc *sc = usbd_xfer_softc(xfer);
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
uvscom_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uvscom_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint8_t buf[2];
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		if (actlen >= 2) {

			pc = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_out(pc, 0, buf, sizeof(buf));

			sc->sc_lsr = 0;
			sc->sc_msr = 0;
			sc->sc_unit_status = buf[1];

			if (buf[0] & UVSCOM_TXRDY) {
				sc->sc_lsr |= ULSR_TXRDY;
			}
			if (buf[0] & UVSCOM_RXRDY) {
				sc->sc_lsr |= ULSR_RXRDY;
			}
			if (buf[1] & UVSCOM_CTS) {
				sc->sc_msr |= SER_CTS;
			}
			if (buf[1] & UVSCOM_DSR) {
				sc->sc_msr |= SER_DSR;
			}
			if (buf[1] & UVSCOM_DCD) {
				sc->sc_msr |= SER_DCD;
			}
			/*
			 * the UCOM layer will ignore this call if the TTY
			 * device is closed!
			 */
			ucom_status_change(&sc->sc_ucom);
		}
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
uvscom_cfg_set_dtr(struct ucom_softc *ucom, uint8_t onoff)
{
	struct uvscom_softc *sc = ucom->sc_parent;

	DPRINTF("onoff = %d\n", onoff);

	if (onoff)
		sc->sc_line |= UVSCOM_DTR;
	else
		sc->sc_line &= ~UVSCOM_DTR;

	uvscom_cfg_write(sc, UVSCOM_LINE_CTL, sc->sc_line);
}

static void
uvscom_cfg_set_rts(struct ucom_softc *ucom, uint8_t onoff)
{
	struct uvscom_softc *sc = ucom->sc_parent;

	DPRINTF("onoff = %d\n", onoff);

	if (onoff)
		sc->sc_line |= UVSCOM_RTS;
	else
		sc->sc_line &= ~UVSCOM_RTS;

	uvscom_cfg_write(sc, UVSCOM_LINE_CTL, sc->sc_line);
}

static void
uvscom_cfg_set_break(struct ucom_softc *ucom, uint8_t onoff)
{
	struct uvscom_softc *sc = ucom->sc_parent;

	DPRINTF("onoff = %d\n", onoff);

	if (onoff)
		sc->sc_line |= UVSCOM_BREAK;
	else
		sc->sc_line &= ~UVSCOM_BREAK;

	uvscom_cfg_write(sc, UVSCOM_LINE_CTL, sc->sc_line);
}

static int
uvscom_pre_param(struct ucom_softc *ucom, struct termios *t)
{
	switch (t->c_ospeed) {
		case B150:
		case B300:
		case B600:
		case B1200:
		case B2400:
		case B4800:
		case B9600:
		case B19200:
		case B38400:
		case B57600:
		case B115200:
		default:
		return (EINVAL);
	}
	return (0);
}

static void
uvscom_cfg_param(struct ucom_softc *ucom, struct termios *t)
{
	struct uvscom_softc *sc = ucom->sc_parent;
	uint16_t value;

	DPRINTF("\n");

	switch (t->c_ospeed) {
	case B150:
		value = UVSCOM_SPEED_150BPS;
		break;
	case B300:
		value = UVSCOM_SPEED_300BPS;
		break;
	case B600:
		value = UVSCOM_SPEED_600BPS;
		break;
	case B1200:
		value = UVSCOM_SPEED_1200BPS;
		break;
	case B2400:
		value = UVSCOM_SPEED_2400BPS;
		break;
	case B4800:
		value = UVSCOM_SPEED_4800BPS;
		break;
	case B9600:
		value = UVSCOM_SPEED_9600BPS;
		break;
	case B19200:
		value = UVSCOM_SPEED_19200BPS;
		break;
	case B38400:
		value = UVSCOM_SPEED_38400BPS;
		break;
	case B57600:
		value = UVSCOM_SPEED_57600BPS;
		break;
	case B115200:
		value = UVSCOM_SPEED_115200BPS;
		break;
	default:
		return;
	}

	uvscom_cfg_write(sc, UVSCOM_SET_SPEED, value);

	value = 0;

	if (t->c_cflag & CSTOPB) {
		value |= UVSCOM_STOP_BIT_2;
	}
	if (t->c_cflag & PARENB) {
		if (t->c_cflag & PARODD) {
			value |= UVSCOM_PARITY_ODD;
		} else {
			value |= UVSCOM_PARITY_EVEN;
		}
	} else {
		value |= UVSCOM_PARITY_NONE;
	}

	switch (t->c_cflag & CSIZE) {
	case CS5:
		value |= UVSCOM_DATA_BIT_5;
		break;
	case CS6:
		value |= UVSCOM_DATA_BIT_6;
		break;
	case CS7:
		value |= UVSCOM_DATA_BIT_7;
		break;
	default:
	case CS8:
		value |= UVSCOM_DATA_BIT_8;
		break;
	}

	uvscom_cfg_write(sc, UVSCOM_SET_PARAM, value);
}

static int
uvscom_pre_open(struct ucom_softc *ucom)
{
	struct uvscom_softc *sc = ucom->sc_parent;

	DPRINTF("sc = %p\n", sc);

	/* check if PC card was inserted */

	if (sc->sc_unit_status & UVSCOM_NOCARD) {
		DPRINTF("no PC card!\n");
		return (ENXIO);
	}
	return (0);
}

static void
uvscom_cfg_open(struct ucom_softc *ucom)
{
	struct uvscom_softc *sc = ucom->sc_parent;

	DPRINTF("sc = %p\n", sc);

	uvscom_cfg_read_status(sc);
}

static void
uvscom_cfg_close(struct ucom_softc *ucom)
{
	struct uvscom_softc *sc = ucom->sc_parent;

	DPRINTF("sc=%p\n", sc);

	uvscom_cfg_write(sc, UVSCOM_SHUTDOWN, 0);
}

static void
uvscom_start_read(struct ucom_softc *ucom)
{
	struct uvscom_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[UVSCOM_BULK_DT_RD]);
}

static void
uvscom_stop_read(struct ucom_softc *ucom)
{
	struct uvscom_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[UVSCOM_BULK_DT_RD]);
}

static void
uvscom_start_write(struct ucom_softc *ucom)
{
	struct uvscom_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[UVSCOM_BULK_DT_WR]);
}

static void
uvscom_stop_write(struct ucom_softc *ucom)
{
	struct uvscom_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[UVSCOM_BULK_DT_WR]);
}

static void
uvscom_cfg_get_status(struct ucom_softc *ucom, uint8_t *lsr, uint8_t *msr)
{
	struct uvscom_softc *sc = ucom->sc_parent;

	*lsr = sc->sc_lsr;
	*msr = sc->sc_msr;
}

static void
uvscom_cfg_write(struct uvscom_softc *sc, uint8_t index, uint16_t value)
{
	struct usb_device_request req;
	usb_error_t err;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = index;
	USETW(req.wValue, value);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, NULL, 0, 1000);
	if (err) {
		DPRINTFN(0, "device request failed, err=%s "
		    "(ignored)\n", usbd_errstr(err));
	}
}

static uint16_t
uvscom_cfg_read_status(struct uvscom_softc *sc)
{
	struct usb_device_request req;
	usb_error_t err;
	uint8_t data[2];

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = UVSCOM_READ_STATUS;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 2);

	err = ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, data, 0, 1000);
	if (err) {
		DPRINTFN(0, "device request failed, err=%s "
		    "(ignored)\n", usbd_errstr(err));
	}
	return (data[0] | (data[1] << 8));
}

static void
uvscom_poll(struct ucom_softc *ucom)
{
	struct uvscom_softc *sc = ucom->sc_parent;
	usbd_transfer_poll(sc->sc_xfer, UVSCOM_N_TRANSFER);
}
