/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2002, Alexander Kabaev <kan.FreeBSD.org>.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ichiro FUKUHARA (ichiro@ichiro.org).
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

#define	USB_DEBUG_VAR ubsa_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/serial/usb_serial.h>

#ifdef USB_DEBUG
static int ubsa_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, ubsa, CTLFLAG_RW, 0, "USB ubsa");
SYSCTL_INT(_hw_usb_ubsa, OID_AUTO, debug, CTLFLAG_RWTUN,
    &ubsa_debug, 0, "ubsa debug level");
#endif

#define	UBSA_BSIZE             1024	/* bytes */

#define	UBSA_CONFIG_INDEX	0
#define	UBSA_IFACE_INDEX	0

#define	UBSA_REG_BAUDRATE	0x00
#define	UBSA_REG_STOP_BITS	0x01
#define	UBSA_REG_DATA_BITS	0x02
#define	UBSA_REG_PARITY		0x03
#define	UBSA_REG_DTR		0x0A
#define	UBSA_REG_RTS		0x0B
#define	UBSA_REG_BREAK		0x0C
#define	UBSA_REG_FLOW_CTRL	0x10

#define	UBSA_PARITY_NONE	0x00
#define	UBSA_PARITY_EVEN	0x01
#define	UBSA_PARITY_ODD		0x02
#define	UBSA_PARITY_MARK	0x03
#define	UBSA_PARITY_SPACE	0x04

#define	UBSA_FLOW_NONE		0x0000
#define	UBSA_FLOW_OCTS		0x0001
#define	UBSA_FLOW_ODSR		0x0002
#define	UBSA_FLOW_IDSR		0x0004
#define	UBSA_FLOW_IDTR		0x0008
#define	UBSA_FLOW_IRTS		0x0010
#define	UBSA_FLOW_ORTS		0x0020
#define	UBSA_FLOW_UNKNOWN	0x0040
#define	UBSA_FLOW_OXON		0x0080
#define	UBSA_FLOW_IXON		0x0100

/* line status register */
#define	UBSA_LSR_TSRE		0x40	/* Transmitter empty: byte sent */
#define	UBSA_LSR_TXRDY		0x20	/* Transmitter buffer empty */
#define	UBSA_LSR_BI		0x10	/* Break detected */
#define	UBSA_LSR_FE		0x08	/* Framing error: bad stop bit */
#define	UBSA_LSR_PE		0x04	/* Parity error */
#define	UBSA_LSR_OE		0x02	/* Overrun, lost incoming byte */
#define	UBSA_LSR_RXRDY		0x01	/* Byte ready in Receive Buffer */
#define	UBSA_LSR_RCV_MASK	0x1f	/* Mask for incoming data or error */

/* modem status register */
/* All deltas are from the last read of the MSR. */
#define	UBSA_MSR_DCD		0x80	/* Current Data Carrier Detect */
#define	UBSA_MSR_RI		0x40	/* Current Ring Indicator */
#define	UBSA_MSR_DSR		0x20	/* Current Data Set Ready */
#define	UBSA_MSR_CTS		0x10	/* Current Clear to Send */
#define	UBSA_MSR_DDCD		0x08	/* DCD has changed state */
#define	UBSA_MSR_TERI		0x04	/* RI has toggled low to high */
#define	UBSA_MSR_DDSR		0x02	/* DSR has changed state */
#define	UBSA_MSR_DCTS		0x01	/* CTS has changed state */

enum {
	UBSA_BULK_DT_WR,
	UBSA_BULK_DT_RD,
	UBSA_INTR_DT_RD,
	UBSA_N_TRANSFER,
};

struct ubsa_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom;

	struct usb_xfer *sc_xfer[UBSA_N_TRANSFER];
	struct usb_device *sc_udev;
	struct mtx sc_mtx;

	uint8_t	sc_iface_no;		/* interface number */
	uint8_t	sc_iface_index;		/* interface index */
	uint8_t	sc_lsr;			/* local status register */
	uint8_t	sc_msr;			/* UBSA status register */
};

static device_probe_t ubsa_probe;
static device_attach_t ubsa_attach;
static device_detach_t ubsa_detach;
static void ubsa_free_softc(struct ubsa_softc *);

static usb_callback_t ubsa_write_callback;
static usb_callback_t ubsa_read_callback;
static usb_callback_t ubsa_intr_callback;

static void	ubsa_cfg_request(struct ubsa_softc *, uint8_t, uint16_t);
static void	ubsa_free(struct ucom_softc *);
static void	ubsa_cfg_set_dtr(struct ucom_softc *, uint8_t);
static void	ubsa_cfg_set_rts(struct ucom_softc *, uint8_t);
static void	ubsa_cfg_set_break(struct ucom_softc *, uint8_t);
static int	ubsa_pre_param(struct ucom_softc *, struct termios *);
static void	ubsa_cfg_param(struct ucom_softc *, struct termios *);
static void	ubsa_start_read(struct ucom_softc *);
static void	ubsa_stop_read(struct ucom_softc *);
static void	ubsa_start_write(struct ucom_softc *);
static void	ubsa_stop_write(struct ucom_softc *);
static void	ubsa_cfg_get_status(struct ucom_softc *, uint8_t *,
		    uint8_t *);
static void	ubsa_poll(struct ucom_softc *ucom);

static const struct usb_config ubsa_config[UBSA_N_TRANSFER] = {

	[UBSA_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = UBSA_BSIZE,	/* bytes */
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &ubsa_write_callback,
	},

	[UBSA_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = UBSA_BSIZE,	/* bytes */
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &ubsa_read_callback,
	},

	[UBSA_INTR_DT_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,	/* use wMaxPacketSize */
		.callback = &ubsa_intr_callback,
	},
};

static const struct ucom_callback ubsa_callback = {
	.ucom_cfg_get_status = &ubsa_cfg_get_status,
	.ucom_cfg_set_dtr = &ubsa_cfg_set_dtr,
	.ucom_cfg_set_rts = &ubsa_cfg_set_rts,
	.ucom_cfg_set_break = &ubsa_cfg_set_break,
	.ucom_cfg_param = &ubsa_cfg_param,
	.ucom_pre_param = &ubsa_pre_param,
	.ucom_start_read = &ubsa_start_read,
	.ucom_stop_read = &ubsa_stop_read,
	.ucom_start_write = &ubsa_start_write,
	.ucom_stop_write = &ubsa_stop_write,
	.ucom_poll = &ubsa_poll,
	.ucom_free = &ubsa_free,
};

static const STRUCT_USB_HOST_ID ubsa_devs[] = {
	/* AnyData ADU-500A */
	{USB_VPI(USB_VENDOR_ANYDATA, USB_PRODUCT_ANYDATA_ADU_500A, 0)},
	/* AnyData ADU-E100A/H */
	{USB_VPI(USB_VENDOR_ANYDATA, USB_PRODUCT_ANYDATA_ADU_E100X, 0)},
	/* Axesstel MV100H */
	{USB_VPI(USB_VENDOR_AXESSTEL, USB_PRODUCT_AXESSTEL_DATAMODEM, 0)},
	/* BELKIN F5U103 */
	{USB_VPI(USB_VENDOR_BELKIN, USB_PRODUCT_BELKIN_F5U103, 0)},
	/* BELKIN F5U120 */
	{USB_VPI(USB_VENDOR_BELKIN, USB_PRODUCT_BELKIN_F5U120, 0)},
	/* GoHubs GO-COM232 */
	{USB_VPI(USB_VENDOR_ETEK, USB_PRODUCT_ETEK_1COM, 0)},
	/* GoHubs GO-COM232 */
	{USB_VPI(USB_VENDOR_GOHUBS, USB_PRODUCT_GOHUBS_GOCOM232, 0)},
	/* Peracom */
	{USB_VPI(USB_VENDOR_PERACOM, USB_PRODUCT_PERACOM_SERIAL1, 0)},
};

static device_method_t ubsa_methods[] = {
	DEVMETHOD(device_probe, ubsa_probe),
	DEVMETHOD(device_attach, ubsa_attach),
	DEVMETHOD(device_detach, ubsa_detach),
	DEVMETHOD_END
};

static devclass_t ubsa_devclass;

static driver_t ubsa_driver = {
	.name = "ubsa",
	.methods = ubsa_methods,
	.size = sizeof(struct ubsa_softc),
};

DRIVER_MODULE(ubsa, uhub, ubsa_driver, ubsa_devclass, NULL, 0);
MODULE_DEPEND(ubsa, ucom, 1, 1, 1);
MODULE_DEPEND(ubsa, usb, 1, 1, 1);
MODULE_VERSION(ubsa, 1);
USB_PNP_HOST_INFO(ubsa_devs);

static int
ubsa_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST) {
		return (ENXIO);
	}
	if (uaa->info.bConfigIndex != UBSA_CONFIG_INDEX) {
		return (ENXIO);
	}
	if (uaa->info.bIfaceIndex != UBSA_IFACE_INDEX) {
		return (ENXIO);
	}
	return (usbd_lookup_id_by_uaa(ubsa_devs, sizeof(ubsa_devs), uaa));
}

static int
ubsa_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct ubsa_softc *sc = device_get_softc(dev);
	int error;

	DPRINTF("sc=%p\n", sc);

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "ubsa", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);

	sc->sc_udev = uaa->device;
	sc->sc_iface_no = uaa->info.bIfaceNum;
	sc->sc_iface_index = UBSA_IFACE_INDEX;

	error = usbd_transfer_setup(uaa->device, &sc->sc_iface_index,
	    sc->sc_xfer, ubsa_config, UBSA_N_TRANSFER, sc, &sc->sc_mtx);

	if (error) {
		DPRINTF("could not allocate all pipes\n");
		goto detach;
	}
	/* clear stall at first run */
	mtx_lock(&sc->sc_mtx);
	usbd_xfer_set_stall(sc->sc_xfer[UBSA_BULK_DT_WR]);
	usbd_xfer_set_stall(sc->sc_xfer[UBSA_BULK_DT_RD]);
	mtx_unlock(&sc->sc_mtx);

	error = ucom_attach(&sc->sc_super_ucom, &sc->sc_ucom, 1, sc,
	    &ubsa_callback, &sc->sc_mtx);
	if (error) {
		DPRINTF("ucom_attach failed\n");
		goto detach;
	}
	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);

	return (0);

detach:
	ubsa_detach(dev);
	return (ENXIO);
}

static int
ubsa_detach(device_t dev)
{
	struct ubsa_softc *sc = device_get_softc(dev);

	DPRINTF("sc=%p\n", sc);

	ucom_detach(&sc->sc_super_ucom, &sc->sc_ucom);
	usbd_transfer_unsetup(sc->sc_xfer, UBSA_N_TRANSFER);

	device_claim_softc(dev);

	ubsa_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(ubsa);

static void
ubsa_free_softc(struct ubsa_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
ubsa_free(struct ucom_softc *ucom)
{
	ubsa_free_softc(ucom->sc_parent);
}

static void
ubsa_cfg_request(struct ubsa_softc *sc, uint8_t index, uint16_t value)
{
	struct usb_device_request req;
	usb_error_t err;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = index;
	USETW(req.wValue, value);
	req.wIndex[0] = sc->sc_iface_no;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);

	err = ucom_cfg_do_request(sc->sc_udev, &sc->sc_ucom, 
	    &req, NULL, 0, 1000);
	if (err) {
		DPRINTFN(0, "device request failed, err=%s "
		    "(ignored)\n", usbd_errstr(err));
	}
}

static void
ubsa_cfg_set_dtr(struct ucom_softc *ucom, uint8_t onoff)
{
	struct ubsa_softc *sc = ucom->sc_parent;

	DPRINTF("onoff = %d\n", onoff);

	ubsa_cfg_request(sc, UBSA_REG_DTR, onoff ? 1 : 0);
}

static void
ubsa_cfg_set_rts(struct ucom_softc *ucom, uint8_t onoff)
{
	struct ubsa_softc *sc = ucom->sc_parent;

	DPRINTF("onoff = %d\n", onoff);

	ubsa_cfg_request(sc, UBSA_REG_RTS, onoff ? 1 : 0);
}

static void
ubsa_cfg_set_break(struct ucom_softc *ucom, uint8_t onoff)
{
	struct ubsa_softc *sc = ucom->sc_parent;

	DPRINTF("onoff = %d\n", onoff);

	ubsa_cfg_request(sc, UBSA_REG_BREAK, onoff ? 1 : 0);
}

static int
ubsa_pre_param(struct ucom_softc *ucom, struct termios *t)
{

	DPRINTF("sc = %p\n", ucom->sc_parent);

	switch (t->c_ospeed) {
	case B0:
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
	case B230400:
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

static void
ubsa_cfg_param(struct ucom_softc *ucom, struct termios *t)
{
	struct ubsa_softc *sc = ucom->sc_parent;
	uint16_t value = 0;

	DPRINTF("sc = %p\n", sc);

	switch (t->c_ospeed) {
	case B0:
		ubsa_cfg_request(sc, UBSA_REG_FLOW_CTRL, 0);
		ubsa_cfg_set_dtr(&sc->sc_ucom, 0);
		ubsa_cfg_set_rts(&sc->sc_ucom, 0);
		break;
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
	case B230400:
		value = B230400 / t->c_ospeed;
		ubsa_cfg_request(sc, UBSA_REG_BAUDRATE, value);
		break;
	default:
		return;
	}

	if (t->c_cflag & PARENB)
		value = (t->c_cflag & PARODD) ? UBSA_PARITY_ODD : UBSA_PARITY_EVEN;
	else
		value = UBSA_PARITY_NONE;

	ubsa_cfg_request(sc, UBSA_REG_PARITY, value);

	switch (t->c_cflag & CSIZE) {
	case CS5:
		value = 0;
		break;
	case CS6:
		value = 1;
		break;
	case CS7:
		value = 2;
		break;
	default:
	case CS8:
		value = 3;
		break;
	}

	ubsa_cfg_request(sc, UBSA_REG_DATA_BITS, value);

	value = (t->c_cflag & CSTOPB) ? 1 : 0;

	ubsa_cfg_request(sc, UBSA_REG_STOP_BITS, value);

	value = 0;
	if (t->c_cflag & CRTSCTS)
		value |= UBSA_FLOW_OCTS | UBSA_FLOW_IRTS;

	if (t->c_iflag & (IXON | IXOFF))
		value |= UBSA_FLOW_OXON | UBSA_FLOW_IXON;

	ubsa_cfg_request(sc, UBSA_REG_FLOW_CTRL, value);
}

static void
ubsa_start_read(struct ucom_softc *ucom)
{
	struct ubsa_softc *sc = ucom->sc_parent;

	/* start interrupt endpoint */
	usbd_transfer_start(sc->sc_xfer[UBSA_INTR_DT_RD]);

	/* start read endpoint */
	usbd_transfer_start(sc->sc_xfer[UBSA_BULK_DT_RD]);
}

static void
ubsa_stop_read(struct ucom_softc *ucom)
{
	struct ubsa_softc *sc = ucom->sc_parent;

	/* stop interrupt endpoint */
	usbd_transfer_stop(sc->sc_xfer[UBSA_INTR_DT_RD]);

	/* stop read endpoint */
	usbd_transfer_stop(sc->sc_xfer[UBSA_BULK_DT_RD]);
}

static void
ubsa_start_write(struct ucom_softc *ucom)
{
	struct ubsa_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[UBSA_BULK_DT_WR]);
}

static void
ubsa_stop_write(struct ucom_softc *ucom)
{
	struct ubsa_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[UBSA_BULK_DT_WR]);
}

static void
ubsa_cfg_get_status(struct ucom_softc *ucom, uint8_t *lsr, uint8_t *msr)
{
	struct ubsa_softc *sc = ucom->sc_parent;

	DPRINTF("\n");

	*lsr = sc->sc_lsr;
	*msr = sc->sc_msr;
}

static void
ubsa_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ubsa_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:
tr_setup:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (ucom_get_data(&sc->sc_ucom, pc, 0,
		    UBSA_BSIZE, &actlen)) {

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
ubsa_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ubsa_softc *sc = usbd_xfer_softc(xfer);
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
ubsa_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ubsa_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint8_t buf[4];
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		if (actlen >= (int)sizeof(buf)) {
			pc = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_out(pc, 0, buf, sizeof(buf));

			/*
			 * MSR bits need translation from ns16550 to SER_* values.
			 * LSR bits are ns16550 in hardware and ucom.
			 */
			sc->sc_msr = 0;
			if (buf[3] & UBSA_MSR_CTS)
				sc->sc_msr |= SER_CTS;
			if (buf[3] & UBSA_MSR_DCD)
				sc->sc_msr |= SER_DCD;
			if (buf[3] & UBSA_MSR_RI)
				sc->sc_msr |= SER_RI;
			if (buf[3] & UBSA_MSR_DSR)
				sc->sc_msr |= SER_DSR;
			sc->sc_lsr = buf[2];

			DPRINTF("lsr = 0x%02x, msr = 0x%02x\n",
			    sc->sc_lsr, sc->sc_msr);

			ucom_status_change(&sc->sc_ucom);
		} else {
			DPRINTF("ignoring short packet, %d bytes\n", actlen);
		}
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
ubsa_poll(struct ucom_softc *ucom)
{
	struct ubsa_softc *sc = ucom->sc_parent;
	usbd_transfer_poll(sc->sc_xfer, UBSA_N_TRANSFER);

}
