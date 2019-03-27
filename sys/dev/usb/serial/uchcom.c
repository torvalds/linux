/*	$NetBSD: uchcom.c,v 1.1 2007/09/03 17:57:37 tshiozak Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2007, Takanori Watanabe
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

/*
 * Copyright (c) 2007 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Takuya SHIOZAKI (tshiozak@netbsd.org).
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
 * Driver for WinChipHead CH341/340, the worst USB-serial chip in the
 * world.
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

#define	USB_DEBUG_VAR uchcom_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/serial/usb_serial.h>

#ifdef USB_DEBUG
static int uchcom_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, uchcom, CTLFLAG_RW, 0, "USB uchcom");
SYSCTL_INT(_hw_usb_uchcom, OID_AUTO, debug, CTLFLAG_RWTUN,
    &uchcom_debug, 0, "uchcom debug level");
#endif

#define	UCHCOM_IFACE_INDEX	0
#define	UCHCOM_CONFIG_INDEX	0

#define	UCHCOM_REV_CH340	0x0250
#define	UCHCOM_INPUT_BUF_SIZE	8

#define	UCHCOM_REQ_GET_VERSION	0x5F
#define	UCHCOM_REQ_READ_REG	0x95
#define	UCHCOM_REQ_WRITE_REG	0x9A
#define	UCHCOM_REQ_RESET	0xA1
#define	UCHCOM_REQ_SET_DTRRTS	0xA4

#define	UCHCOM_REG_STAT1	0x06
#define	UCHCOM_REG_STAT2	0x07
#define	UCHCOM_REG_BPS_PRE	0x12
#define	UCHCOM_REG_BPS_DIV	0x13
#define	UCHCOM_REG_BPS_MOD	0x14
#define	UCHCOM_REG_BPS_PAD	0x0F
#define	UCHCOM_REG_BREAK1	0x05
#define	UCHCOM_REG_LCR1		0x18
#define	UCHCOM_REG_LCR2		0x25

#define	UCHCOM_VER_20		0x20
#define	UCHCOM_VER_30		0x30

#define	UCHCOM_BASE_UNKNOWN	0
#define	UCHCOM_BPS_MOD_BASE	20000000
#define	UCHCOM_BPS_MOD_BASE_OFS	1100

#define	UCHCOM_DTR_MASK		0x20
#define	UCHCOM_RTS_MASK		0x40

#define	UCHCOM_BRK_MASK		0x01

#define	UCHCOM_LCR1_MASK	0xAF
#define	UCHCOM_LCR2_MASK	0x07
#define	UCHCOM_LCR1_RX		0x80
#define	UCHCOM_LCR1_TX		0x40
#define	UCHCOM_LCR1_PARENB	0x08
#define	UCHCOM_LCR1_CS8		0x03
#define	UCHCOM_LCR2_PAREVEN	0x07
#define	UCHCOM_LCR2_PARODD	0x06
#define	UCHCOM_LCR2_PARMARK	0x05
#define	UCHCOM_LCR2_PARSPACE	0x04

#define	UCHCOM_INTR_STAT1	0x02
#define	UCHCOM_INTR_STAT2	0x03
#define	UCHCOM_INTR_LEAST	4

#define	UCHCOM_BULK_BUF_SIZE 1024	/* bytes */

enum {
	UCHCOM_BULK_DT_WR,
	UCHCOM_BULK_DT_RD,
	UCHCOM_INTR_DT_RD,
	UCHCOM_N_TRANSFER,
};

struct uchcom_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom;

	struct usb_xfer *sc_xfer[UCHCOM_N_TRANSFER];
	struct usb_device *sc_udev;
	struct mtx sc_mtx;

	uint8_t	sc_dtr;			/* local copy */
	uint8_t	sc_rts;			/* local copy */
	uint8_t	sc_version;
	uint8_t	sc_msr;
	uint8_t	sc_lsr;			/* local status register */
};

struct uchcom_divider {
	uint8_t	dv_prescaler;
	uint8_t	dv_div;
	uint8_t	dv_mod;
};

struct uchcom_divider_record {
	uint32_t dvr_high;
	uint32_t dvr_low;
	uint32_t dvr_base_clock;
	struct uchcom_divider dvr_divider;
};

static const struct uchcom_divider_record dividers[] =
{
	{307200, 307200, UCHCOM_BASE_UNKNOWN, {7, 0xD9, 0}},
	{921600, 921600, UCHCOM_BASE_UNKNOWN, {7, 0xF3, 0}},
	{2999999, 23530, 6000000, {3, 0, 0}},
	{23529, 2942, 750000, {2, 0, 0}},
	{2941, 368, 93750, {1, 0, 0}},
	{367, 1, 11719, {0, 0, 0}},
};

#define	NUM_DIVIDERS	nitems(dividers)

static const STRUCT_USB_HOST_ID uchcom_devs[] = {
	{USB_VPI(USB_VENDOR_WCH, USB_PRODUCT_WCH_CH341SER, 0)},
	{USB_VPI(USB_VENDOR_WCH2, USB_PRODUCT_WCH2_CH341SER, 0)},
	{USB_VPI(USB_VENDOR_WCH2, USB_PRODUCT_WCH2_CH341SER_2, 0)},
};

/* protypes */

static void	uchcom_free(struct ucom_softc *);
static int	uchcom_pre_param(struct ucom_softc *, struct termios *);
static void	uchcom_cfg_get_status(struct ucom_softc *, uint8_t *,
		    uint8_t *);
static void	uchcom_cfg_open(struct ucom_softc *ucom);
static void	uchcom_cfg_param(struct ucom_softc *, struct termios *);
static void	uchcom_cfg_set_break(struct ucom_softc *, uint8_t);
static void	uchcom_cfg_set_dtr(struct ucom_softc *, uint8_t);
static void	uchcom_cfg_set_rts(struct ucom_softc *, uint8_t);
static void	uchcom_start_read(struct ucom_softc *);
static void	uchcom_start_write(struct ucom_softc *);
static void	uchcom_stop_read(struct ucom_softc *);
static void	uchcom_stop_write(struct ucom_softc *);
static void	uchcom_update_version(struct uchcom_softc *);
static void	uchcom_convert_status(struct uchcom_softc *, uint8_t);
static void	uchcom_update_status(struct uchcom_softc *);
static void	uchcom_set_dtr_rts(struct uchcom_softc *);
static int	uchcom_calc_divider_settings(struct uchcom_divider *, uint32_t);
static void	uchcom_set_baudrate(struct uchcom_softc *, uint32_t);
static void	uchcom_poll(struct ucom_softc *ucom);

static device_probe_t uchcom_probe;
static device_attach_t uchcom_attach;
static device_detach_t uchcom_detach;
static void uchcom_free_softc(struct uchcom_softc *);

static usb_callback_t uchcom_intr_callback;
static usb_callback_t uchcom_write_callback;
static usb_callback_t uchcom_read_callback;

static const struct usb_config uchcom_config_data[UCHCOM_N_TRANSFER] = {

	[UCHCOM_BULK_DT_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = UCHCOM_BULK_BUF_SIZE,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,},
		.callback = &uchcom_write_callback,
	},

	[UCHCOM_BULK_DT_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.bufsize = UCHCOM_BULK_BUF_SIZE,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.callback = &uchcom_read_callback,
	},

	[UCHCOM_INTR_DT_RD] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,	/* use wMaxPacketSize */
		.callback = &uchcom_intr_callback,
	},
};

static struct ucom_callback uchcom_callback = {
	.ucom_cfg_get_status = &uchcom_cfg_get_status,
	.ucom_cfg_set_dtr = &uchcom_cfg_set_dtr,
	.ucom_cfg_set_rts = &uchcom_cfg_set_rts,
	.ucom_cfg_set_break = &uchcom_cfg_set_break,
	.ucom_cfg_open = &uchcom_cfg_open,
	.ucom_cfg_param = &uchcom_cfg_param,
	.ucom_pre_param = &uchcom_pre_param,
	.ucom_start_read = &uchcom_start_read,
	.ucom_stop_read = &uchcom_stop_read,
	.ucom_start_write = &uchcom_start_write,
	.ucom_stop_write = &uchcom_stop_write,
	.ucom_poll = &uchcom_poll,
	.ucom_free = &uchcom_free,
};

/* ----------------------------------------------------------------------
 * driver entry points
 */

static int
uchcom_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	DPRINTFN(11, "\n");

	if (uaa->usb_mode != USB_MODE_HOST) {
		return (ENXIO);
	}
	if (uaa->info.bConfigIndex != UCHCOM_CONFIG_INDEX) {
		return (ENXIO);
	}
	if (uaa->info.bIfaceIndex != UCHCOM_IFACE_INDEX) {
		return (ENXIO);
	}
	return (usbd_lookup_id_by_uaa(uchcom_devs, sizeof(uchcom_devs), uaa));
}

static int
uchcom_attach(device_t dev)
{
	struct uchcom_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	int error;
	uint8_t iface_index;

	DPRINTFN(11, "\n");

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "uchcom", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);

	sc->sc_udev = uaa->device;

	switch (uaa->info.idProduct) {
	case USB_PRODUCT_WCH2_CH341SER:
		device_printf(dev, "CH340 detected\n");
		break;
	case USB_PRODUCT_WCH2_CH341SER_2:
		device_printf(dev, "CH341 detected\n");
		break;
	default:
		device_printf(dev, "New CH340/CH341 product 0x%04x detected\n",
		    uaa->info.idProduct);
		break;
	}

	iface_index = UCHCOM_IFACE_INDEX;
	error = usbd_transfer_setup(uaa->device,
	    &iface_index, sc->sc_xfer, uchcom_config_data,
	    UCHCOM_N_TRANSFER, sc, &sc->sc_mtx);

	if (error) {
		DPRINTF("one or more missing USB endpoints, "
		    "error=%s\n", usbd_errstr(error));
		goto detach;
	}

	/* clear stall at first run */
	mtx_lock(&sc->sc_mtx);
	usbd_xfer_set_stall(sc->sc_xfer[UCHCOM_BULK_DT_WR]);
	usbd_xfer_set_stall(sc->sc_xfer[UCHCOM_BULK_DT_RD]);
	mtx_unlock(&sc->sc_mtx);

	error = ucom_attach(&sc->sc_super_ucom, &sc->sc_ucom, 1, sc,
	    &uchcom_callback, &sc->sc_mtx);
	if (error) {
		goto detach;
	}
	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);

	return (0);

detach:
	uchcom_detach(dev);
	return (ENXIO);
}

static int
uchcom_detach(device_t dev)
{
	struct uchcom_softc *sc = device_get_softc(dev);

	DPRINTFN(11, "\n");

	ucom_detach(&sc->sc_super_ucom, &sc->sc_ucom);
	usbd_transfer_unsetup(sc->sc_xfer, UCHCOM_N_TRANSFER);

	device_claim_softc(dev);

	uchcom_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(uchcom);

static void
uchcom_free_softc(struct uchcom_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
uchcom_free(struct ucom_softc *ucom)
{
	uchcom_free_softc(ucom->sc_parent);
}

/* ----------------------------------------------------------------------
 * low level i/o
 */

static void
uchcom_ctrl_write(struct uchcom_softc *sc, uint8_t reqno,
    uint16_t value, uint16_t index)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = reqno;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, 0);

	DPRINTF("WR REQ 0x%02X VAL 0x%04X IDX 0x%04X\n",
	    reqno, value, index);
	ucom_cfg_do_request(sc->sc_udev,
	    &sc->sc_ucom, &req, NULL, 0, 1000);
}

static void
uchcom_ctrl_read(struct uchcom_softc *sc, uint8_t reqno,
    uint16_t value, uint16_t index, void *buf, uint16_t buflen)
{
	struct usb_device_request req;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = reqno;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, buflen);

	DPRINTF("RD REQ 0x%02X VAL 0x%04X IDX 0x%04X LEN %d\n",
	    reqno, value, index, buflen);
	ucom_cfg_do_request(sc->sc_udev,
	    &sc->sc_ucom, &req, buf, USB_SHORT_XFER_OK, 1000);
}

static void
uchcom_write_reg(struct uchcom_softc *sc,
    uint8_t reg1, uint8_t val1, uint8_t reg2, uint8_t val2)
{
	DPRINTF("0x%02X<-0x%02X, 0x%02X<-0x%02X\n",
	    (unsigned)reg1, (unsigned)val1,
	    (unsigned)reg2, (unsigned)val2);
	uchcom_ctrl_write(
	    sc, UCHCOM_REQ_WRITE_REG,
	    reg1 | ((uint16_t)reg2 << 8), val1 | ((uint16_t)val2 << 8));
}

static void
uchcom_read_reg(struct uchcom_softc *sc,
    uint8_t reg1, uint8_t *rval1, uint8_t reg2, uint8_t *rval2)
{
	uint8_t buf[UCHCOM_INPUT_BUF_SIZE];

	uchcom_ctrl_read(
	    sc, UCHCOM_REQ_READ_REG,
	    reg1 | ((uint16_t)reg2 << 8), 0, buf, sizeof(buf));

	DPRINTF("0x%02X->0x%02X, 0x%02X->0x%02X\n",
	    (unsigned)reg1, (unsigned)buf[0],
	    (unsigned)reg2, (unsigned)buf[1]);

	if (rval1)
		*rval1 = buf[0];
	if (rval2)
		*rval2 = buf[1];
}

static void
uchcom_get_version(struct uchcom_softc *sc, uint8_t *rver)
{
	uint8_t buf[UCHCOM_INPUT_BUF_SIZE];

	uchcom_ctrl_read(sc, UCHCOM_REQ_GET_VERSION, 0, 0, buf, sizeof(buf));

	if (rver)
		*rver = buf[0];
}

static void
uchcom_get_status(struct uchcom_softc *sc, uint8_t *rval)
{
	uchcom_read_reg(sc, UCHCOM_REG_STAT1, rval, UCHCOM_REG_STAT2, NULL);
}

static void
uchcom_set_dtr_rts_10(struct uchcom_softc *sc, uint8_t val)
{
	uchcom_write_reg(sc, UCHCOM_REG_STAT1, val, UCHCOM_REG_STAT1, val);
}

static void
uchcom_set_dtr_rts_20(struct uchcom_softc *sc, uint8_t val)
{
	uchcom_ctrl_write(sc, UCHCOM_REQ_SET_DTRRTS, val, 0);
}


/* ----------------------------------------------------------------------
 * middle layer
 */

static void
uchcom_update_version(struct uchcom_softc *sc)
{
	uchcom_get_version(sc, &sc->sc_version);
	DPRINTF("Chip version: 0x%02x\n", sc->sc_version);
}

static void
uchcom_convert_status(struct uchcom_softc *sc, uint8_t cur)
{
	sc->sc_dtr = !(cur & UCHCOM_DTR_MASK);
	sc->sc_rts = !(cur & UCHCOM_RTS_MASK);

	cur = ~cur & 0x0F;
	sc->sc_msr = (cur << 4) | ((sc->sc_msr >> 4) ^ cur);
}

static void
uchcom_update_status(struct uchcom_softc *sc)
{
	uint8_t cur;

	uchcom_get_status(sc, &cur);
	uchcom_convert_status(sc, cur);
}


static void
uchcom_set_dtr_rts(struct uchcom_softc *sc)
{
	uint8_t val = 0;

	if (sc->sc_dtr)
		val |= UCHCOM_DTR_MASK;
	if (sc->sc_rts)
		val |= UCHCOM_RTS_MASK;

	if (sc->sc_version < UCHCOM_VER_20)
		uchcom_set_dtr_rts_10(sc, ~val);
	else
		uchcom_set_dtr_rts_20(sc, ~val);
}

static void
uchcom_cfg_set_break(struct ucom_softc *ucom, uint8_t onoff)
{
	struct uchcom_softc *sc = ucom->sc_parent;
	uint8_t brk1;
	uint8_t brk2;

	uchcom_read_reg(sc, UCHCOM_REG_BREAK1, &brk1, UCHCOM_REG_LCR1, &brk2);
	if (onoff) {
		/* on - clear bits */
		brk1 &= ~UCHCOM_BRK_MASK;
		brk2 &= ~UCHCOM_LCR1_TX;
	} else {
		/* off - set bits */
		brk1 |= UCHCOM_BRK_MASK;
		brk2 |= UCHCOM_LCR1_TX;
	}
	uchcom_write_reg(sc, UCHCOM_REG_BREAK1, brk1, UCHCOM_REG_LCR1, brk2);
}

static int
uchcom_calc_divider_settings(struct uchcom_divider *dp, uint32_t rate)
{
	const struct uchcom_divider_record *rp;
	uint32_t div;
	uint32_t rem;
	uint32_t mod;
	uint8_t i;

	/* find record */
	for (i = 0; i != NUM_DIVIDERS; i++) {
		if (dividers[i].dvr_high >= rate &&
		    dividers[i].dvr_low <= rate) {
			rp = &dividers[i];
			goto found;
		}
	}
	return (-1);

found:
	dp->dv_prescaler = rp->dvr_divider.dv_prescaler;
	if (rp->dvr_base_clock == UCHCOM_BASE_UNKNOWN)
		dp->dv_div = rp->dvr_divider.dv_div;
	else {
		div = rp->dvr_base_clock / rate;
		rem = rp->dvr_base_clock % rate;
		if (div == 0 || div >= 0xFF)
			return (-1);
		if ((rem << 1) >= rate)
			div += 1;
		dp->dv_div = (uint8_t)-div;
	}

	mod = (UCHCOM_BPS_MOD_BASE / rate) + UCHCOM_BPS_MOD_BASE_OFS;
	mod = mod + (mod / 2);

	dp->dv_mod = (mod + 0xFF) / 0x100;

	return (0);
}

static void
uchcom_set_baudrate(struct uchcom_softc *sc, uint32_t rate)
{
	struct uchcom_divider dv;

	if (uchcom_calc_divider_settings(&dv, rate))
		return;

	/*
	 * According to linux code we need to set bit 7 of UCHCOM_REG_BPS_PRE,
	 * otherwise the chip will buffer data.
	 */
	uchcom_write_reg(sc,
	    UCHCOM_REG_BPS_PRE, dv.dv_prescaler | 0x80,
	    UCHCOM_REG_BPS_DIV, dv.dv_div);
	uchcom_write_reg(sc,
	    UCHCOM_REG_BPS_MOD, dv.dv_mod,
	    UCHCOM_REG_BPS_PAD, 0);
}

/* ----------------------------------------------------------------------
 * methods for ucom
 */
static void
uchcom_cfg_get_status(struct ucom_softc *ucom, uint8_t *lsr, uint8_t *msr)
{
	struct uchcom_softc *sc = ucom->sc_parent;

	DPRINTF("\n");

	/* XXX Note: sc_lsr is always zero */
	*lsr = sc->sc_lsr;
	*msr = sc->sc_msr;
}

static void
uchcom_cfg_set_dtr(struct ucom_softc *ucom, uint8_t onoff)
{
	struct uchcom_softc *sc = ucom->sc_parent;

	DPRINTF("onoff = %d\n", onoff);

	sc->sc_dtr = onoff;
	uchcom_set_dtr_rts(sc);
}

static void
uchcom_cfg_set_rts(struct ucom_softc *ucom, uint8_t onoff)
{
	struct uchcom_softc *sc = ucom->sc_parent;

	DPRINTF("onoff = %d\n", onoff);

	sc->sc_rts = onoff;
	uchcom_set_dtr_rts(sc);
}

static void
uchcom_cfg_open(struct ucom_softc *ucom)
{
	struct uchcom_softc *sc = ucom->sc_parent;

	DPRINTF("\n");

	uchcom_update_version(sc);
	uchcom_update_status(sc);
}

static int
uchcom_pre_param(struct ucom_softc *ucom, struct termios *t)
{
	struct uchcom_divider dv;

	switch (t->c_cflag & CSIZE) {
	case CS8:
		break;
	default:
		return (EIO);
	}
	if ((t->c_cflag & CSTOPB) != 0)
		return (EIO);
	if ((t->c_cflag & PARENB) != 0)
		return (EIO);

	if (uchcom_calc_divider_settings(&dv, t->c_ospeed)) {
		return (EIO);
	}
	return (0);			/* success */
}

static void
uchcom_cfg_param(struct ucom_softc *ucom, struct termios *t)
{
	struct uchcom_softc *sc = ucom->sc_parent;

	uchcom_get_version(sc, NULL);
	uchcom_ctrl_write(sc, UCHCOM_REQ_RESET, 0, 0);
	uchcom_set_baudrate(sc, t->c_ospeed);
	if (sc->sc_version < UCHCOM_VER_30) {
		uchcom_read_reg(sc, UCHCOM_REG_LCR1, NULL,
		    UCHCOM_REG_LCR2, NULL);
		uchcom_write_reg(sc, UCHCOM_REG_LCR1, 0x50,
		    UCHCOM_REG_LCR2, 0x00);
	} else {
		/*
		 * Set up line control:
		 * - enable transmit and receive
		 * - set 8n1 mode
		 * To do: support other sizes, parity, stop bits.
		 */
		uchcom_write_reg(sc,
		    UCHCOM_REG_LCR1,
		    UCHCOM_LCR1_RX | UCHCOM_LCR1_TX | UCHCOM_LCR1_CS8,
		    UCHCOM_REG_LCR2, 0x00);
	}
	uchcom_update_status(sc);
	uchcom_ctrl_write(sc, UCHCOM_REQ_RESET, 0x501f, 0xd90a);
	uchcom_set_baudrate(sc, t->c_ospeed);
	uchcom_set_dtr_rts(sc);
	uchcom_update_status(sc);
}

static void
uchcom_start_read(struct ucom_softc *ucom)
{
	struct uchcom_softc *sc = ucom->sc_parent;

	/* start interrupt endpoint */
	usbd_transfer_start(sc->sc_xfer[UCHCOM_INTR_DT_RD]);

	/* start read endpoint */
	usbd_transfer_start(sc->sc_xfer[UCHCOM_BULK_DT_RD]);
}

static void
uchcom_stop_read(struct ucom_softc *ucom)
{
	struct uchcom_softc *sc = ucom->sc_parent;

	/* stop interrupt endpoint */
	usbd_transfer_stop(sc->sc_xfer[UCHCOM_INTR_DT_RD]);

	/* stop read endpoint */
	usbd_transfer_stop(sc->sc_xfer[UCHCOM_BULK_DT_RD]);
}

static void
uchcom_start_write(struct ucom_softc *ucom)
{
	struct uchcom_softc *sc = ucom->sc_parent;

	usbd_transfer_start(sc->sc_xfer[UCHCOM_BULK_DT_WR]);
}

static void
uchcom_stop_write(struct ucom_softc *ucom)
{
	struct uchcom_softc *sc = ucom->sc_parent;

	usbd_transfer_stop(sc->sc_xfer[UCHCOM_BULK_DT_WR]);
}

/* ----------------------------------------------------------------------
 * callback when the modem status is changed.
 */
static void
uchcom_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uchcom_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint8_t buf[UCHCOM_INTR_LEAST];
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		DPRINTF("actlen = %u\n", actlen);

		if (actlen >= UCHCOM_INTR_LEAST) {
			pc = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_out(pc, 0, buf, UCHCOM_INTR_LEAST);

			DPRINTF("data = 0x%02X 0x%02X 0x%02X 0x%02X\n",
			    (unsigned)buf[0], (unsigned)buf[1],
			    (unsigned)buf[2], (unsigned)buf[3]);

			uchcom_convert_status(sc, buf[UCHCOM_INTR_STAT1]);
			ucom_status_change(&sc->sc_ucom);
		}
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
uchcom_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uchcom_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint32_t actlen;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:
tr_setup:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (ucom_get_data(&sc->sc_ucom, pc, 0,
		    usbd_xfer_max_len(xfer), &actlen)) {

			DPRINTF("actlen = %d\n", actlen);

			usbd_xfer_set_frame_len(xfer, 0, actlen);
			usbd_transfer_submit(xfer);
		}
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
uchcom_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uchcom_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		if (actlen > 0) {
			pc = usbd_xfer_get_frame(xfer, 0);
			ucom_put_data(&sc->sc_ucom, pc, 0, actlen);
		}

	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
uchcom_poll(struct ucom_softc *ucom)
{
	struct uchcom_softc *sc = ucom->sc_parent;
	usbd_transfer_poll(sc->sc_xfer, UCHCOM_N_TRANSFER);
}

static device_method_t uchcom_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, uchcom_probe),
	DEVMETHOD(device_attach, uchcom_attach),
	DEVMETHOD(device_detach, uchcom_detach),
	DEVMETHOD_END
};

static driver_t uchcom_driver = {
	.name = "uchcom",
	.methods = uchcom_methods,
	.size = sizeof(struct uchcom_softc)
};

static devclass_t uchcom_devclass;

DRIVER_MODULE(uchcom, uhub, uchcom_driver, uchcom_devclass, NULL, 0);
MODULE_DEPEND(uchcom, ucom, 1, 1, 1);
MODULE_DEPEND(uchcom, usb, 1, 1, 1);
MODULE_VERSION(uchcom, 1);
USB_PNP_HOST_INFO(uchcom_devs);
