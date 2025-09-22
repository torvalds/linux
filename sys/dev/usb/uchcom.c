/*	$OpenBSD: uchcom.c,v 1.39 2025/08/19 00:47:43 kevlo Exp $	*/
/*	$NetBSD: uchcom.c,v 1.1 2007/09/03 17:57:37 tshiozak Exp $	*/

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

/*
 * driver for WinChipHead CH9102/343/341/340.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/ucomvar.h>

#ifdef UCHCOM_DEBUG
#define DPRINTFN(n, x)  do { if (uchcomdebug > (n)) printf x; } while (0)
int	uchcomdebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x) DPRINTFN(0, x)

#define	UCHCOM_IFACE_INDEX		0
#define UCHCOM_SECOND_IFACE_INDEX	1

#define UCHCOM_REV_CH340	0x0250
#define UCHCOM_REV_CH343	0x0440
#define UCHCOM_INPUT_BUF_SIZE	8

#define UCHCOM_REQ_GET_VERSION		0x5F
#define UCHCOM_REQ_READ_REG		0x95
#define UCHCOM_REQ_WRITE_REG		0x9A
#define UCHCOM_REQ_RESET		0xA1
#define UCHCOM_REQ_SET_DTRRTS		0xA4
#define UCHCOM_REQ_CH343_WRITE_REG	0xA8
#define UCHCOM_REQ_SET_BAUDRATE		UCHCOM_REQ_RESET

#define UCHCOM_REG_STAT1	0x06
#define UCHCOM_REG_STAT2	0x07
#define UCHCOM_REG_BPS_PRE	0x12
#define UCHCOM_REG_BPS_DIV	0x13
#define UCHCOM_REG_BPS_MOD	0x14
#define UCHCOM_REG_BPS_PAD	0x0F
#define UCHCOM_REG_BREAK	0x05
#define UCHCOM_REG_LCR		0x18
#define UCHCOM_REG_LCR2		0x25

#define UCHCOM_VER_20		0x20

#define UCHCOM_BASE_UNKNOWN	0
#define UCHCOM_BPS_MOD_BASE	20000000
#define UCHCOM_BPS_MOD_BASE_OFS	1100

#define UCHCOM_BPS_PRE_IMM	0x80	/* CH341: immediate RX forwarding */

#define UCHCOM_DTR_MASK		0x20
#define UCHCOM_RTS_MASK		0x40

#define UCHCOM_BREAK_MASK	0x01
#define UCHCOM_ABREAK_MASK	0x10
#define UCHCOM_CH343_BREAK_MASK	0x80

#define UCHCOM_LCR_CS5		0x00
#define UCHCOM_LCR_CS6		0x01
#define UCHCOM_LCR_CS7		0x02
#define UCHCOM_LCR_CS8		0x03
#define UCHCOM_LCR_STOPB	0x04
#define UCHCOM_LCR_PARENB	0x08
#define UCHCOM_LCR_PARODD	0x00
#define UCHCOM_LCR_PAREVEN	0x10
#define UCHCOM_LCR_PARMARK	0x20
#define UCHCOM_LCR_PARSPACE	0x30
#define UCHCOM_LCR_TXE		0x40
#define UCHCOM_LCR_RXE		0x80

#define UCHCOM_INTR_STAT1	0x02
#define UCHCOM_INTR_STAT2	0x03
#define UCHCOM_INTR_LEAST	4

#define UCHCOM_T		0x08
#define UCHCOM_CL		0x04
#define UCHCOM_CT		0x80

#define UCHCOMOBUFSIZE	256

#define UCHCOM_TYPE_CH343	1

struct uchcom_softc {
	struct device		 sc_dev;
	struct usbd_device	*sc_udev;
	struct device		*sc_subdev;
	struct usbd_interface	*sc_intr_iface;
	struct usbd_interface	*sc_data_iface;
	/* */
	int			 sc_intr_endpoint;
	struct usbd_pipe	*sc_intr_pipe;
	u_char			*sc_intr_buf;
	int			 sc_isize;
	/* */
	int			 sc_release;
	uint8_t			 sc_version;
	int			 sc_type;
	int			 sc_dtr;
	int			 sc_rts;
	u_char			 sc_lsr;
	u_char			 sc_msr;
	int			 sc_lcr1;
	int			 sc_lcr2;
};

struct uchcom_endpoints {
	int		ep_bulkin;
	int		ep_bulkin_size;
	int		ep_bulkout;
	int		ep_intr;
	int		ep_intr_size;
};

void		uchcom_get_status(void *, int, u_char *, u_char *);
void		uchcom_set(void *, int, int, int);
int		uchcom_param(void *, int, struct termios *);
int		uchcom_open(void *, int);
void		uchcom_close(void *, int);
void		uchcom_intr(struct usbd_xfer *, void *, usbd_status);

int		uchcom_find_endpoints(struct uchcom_softc *,
		    struct uchcom_endpoints *);
void		uchcom_close_intr_pipe(struct uchcom_softc *);


usbd_status	uchcom_generic_control_out(struct uchcom_softc *sc,
		    uint8_t reqno, uint16_t value, uint16_t index);
usbd_status	uchcom_generic_control_in(struct uchcom_softc *, uint8_t,
		    uint16_t, uint16_t, void *, int, int *);
usbd_status	uchcom_write_reg(struct uchcom_softc *, uint8_t, uint8_t,
		    uint8_t, uint8_t);
usbd_status	uchcom_read_reg(struct uchcom_softc *, uint8_t, uint8_t *,
		    uint8_t, uint8_t *);
usbd_status	uchcom_get_version(struct uchcom_softc *, uint8_t *);
usbd_status	uchcom_read_status(struct uchcom_softc *, uint8_t *);
usbd_status	uchcom_set_dtrrts_10(struct uchcom_softc *, uint8_t);
usbd_status	uchcom_set_dtrrts_20(struct uchcom_softc *, uint8_t);
int		uchcom_update_version(struct uchcom_softc *);
void		uchcom_convert_status(struct uchcom_softc *, uint8_t);
int		uchcom_update_status(struct uchcom_softc *);
int		uchcom_set_dtrrts(struct uchcom_softc *, int, int);
int		uchcom_set_break(struct uchcom_softc *, int);
int		uchcom_set_break_ch343(struct uchcom_softc *, int);
void		uchcom_calc_baudrate(struct uchcom_softc *, uint32_t, uint8_t *,
		    uint8_t *);
int		uchcom_set_dte_rate(struct uchcom_softc *, uint32_t, uint16_t);
uint16_t	uchcom_set_line_control(struct uchcom_softc *, tcflag_t,
		    uint16_t *);
int		uchcom_clear_chip(struct uchcom_softc *);
int		uchcom_setup_intr_pipe(struct uchcom_softc *);


int		uchcom_match(struct device *, void *, void *);
void		uchcom_attach(struct device *, struct device *, void *);
int		uchcom_detach(struct device *, int);

const struct ucom_methods uchcom_methods = {
	uchcom_get_status,
	uchcom_set,
	uchcom_param,
	NULL,
	uchcom_open,
	uchcom_close,
	NULL,
	NULL,
};

static const struct usb_devno uchcom_devs[] = {
	{ USB_VENDOR_WCH, USB_PRODUCT_WCH_CH341 },
	{ USB_VENDOR_WCH2, USB_PRODUCT_WCH2_CH340 },
	{ USB_VENDOR_WCH2, USB_PRODUCT_WCH2_CH340K },
	{ USB_VENDOR_WCH2, USB_PRODUCT_WCH2_CH341A },
	{ USB_VENDOR_WCH2, USB_PRODUCT_WCH2_CH343 },
	{ USB_VENDOR_WCH2, USB_PRODUCT_WCH2_CH9102 }
};

struct cfdriver uchcom_cd = {
	NULL, "uchcom", DV_DULL
};

const struct cfattach uchcom_ca = {
	sizeof(struct uchcom_softc), uchcom_match, uchcom_attach, uchcom_detach
};

/* ----------------------------------------------------------------------
 * driver entry points
 */

int
uchcom_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL)
		return UMATCH_NONE;

	return (usb_lookup(uchcom_devs, uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
uchcom_attach(struct device *parent, struct device *self, void *aux)
{
	struct uchcom_softc *sc = (struct uchcom_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct ucom_attach_args uca;
	struct uchcom_endpoints endpoints;

	sc->sc_udev = uaa->device;
	sc->sc_intr_iface = uaa->iface;
	sc->sc_dtr = sc->sc_rts = -1;
	sc->sc_release = uaa->release;

	DPRINTF(("\n\nuchcom attach: sc=%p\n", sc));

	if (sc->sc_release >= UCHCOM_REV_CH343) {
		printf("%s: CH343\n", sc->sc_dev.dv_xname);
		sc->sc_type = UCHCOM_TYPE_CH343;
	} else if (sc->sc_release == UCHCOM_REV_CH340)
		printf("%s: CH340\n", sc->sc_dev.dv_xname);
	else
		printf("%s: CH341\n", sc->sc_dev.dv_xname);

	if (uchcom_find_endpoints(sc, &endpoints))
		goto failed;

	sc->sc_intr_endpoint = endpoints.ep_intr;
	sc->sc_isize = endpoints.ep_intr_size;

	/* setup ucom layer */
	uca.portno = UCOM_UNK_PORTNO;
	uca.bulkin = endpoints.ep_bulkin;
	uca.bulkout = endpoints.ep_bulkout;
	uca.ibufsize = endpoints.ep_bulkin_size;
	uca.obufsize = UCHCOMOBUFSIZE;
	uca.ibufsizepad = endpoints.ep_bulkin_size;
	uca.opkthdrlen = 0;
	uca.device = sc->sc_udev;
	uca.iface = sc->sc_data_iface;
	uca.methods = &uchcom_methods;
	uca.arg = sc;
	uca.info = NULL;

	sc->sc_subdev = config_found_sm(self, &uca, ucomprint, ucomsubmatch);

	return;

failed:
	usbd_deactivate(sc->sc_udev);
}

int
uchcom_detach(struct device *self, int flags)
{
	struct uchcom_softc *sc = (struct uchcom_softc *)self;
	int rv = 0;

	DPRINTF(("uchcom_detach: sc=%p flags=%d\n", sc, flags));

	uchcom_close_intr_pipe(sc);

	if (sc->sc_subdev != NULL) {
		rv = config_detach(sc->sc_subdev, flags);
		sc->sc_subdev = NULL;
	}

	return rv;
}

int
uchcom_find_endpoints(struct uchcom_softc *sc,
    struct uchcom_endpoints *endpoints)
{
	int i, bin=-1, bout=-1, intr=-1, binsize=0, isize=0;
	usb_config_descriptor_t *cdesc;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	usbd_status err;
	uint8_t ifaceno;

	/* Get the config descriptor. */
	cdesc = usbd_get_config_descriptor(sc->sc_udev);

	if (cdesc == NULL) {
		printf("%s: failed to get configuration descriptor\n",
		    sc->sc_dev.dv_xname);
		return -1;
        }

	id = usbd_get_interface_descriptor(sc->sc_intr_iface);

	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_intr_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor for %d\n",
				sc->sc_dev.dv_xname, i);
			return -1;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			intr = ed->bEndpointAddress;
			isize = UGETW(ed->wMaxPacketSize);
		}
	}

	ifaceno = (cdesc->bNumInterfaces == 2) ?
	    UCHCOM_SECOND_IFACE_INDEX : UCHCOM_IFACE_INDEX;

	err = usbd_device2interface_handle(sc->sc_udev, ifaceno,
	    &sc->sc_data_iface);
	if (err) {
		printf("\n%s: failed to get second interface, err=%s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return -1;
	}

	id = usbd_get_interface_descriptor(sc->sc_data_iface);

	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_data_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor for %d\n",
				sc->sc_dev.dv_xname, i);
			return -1;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			bin = ed->bEndpointAddress;
			binsize = UGETW(ed->wMaxPacketSize);
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			bout = ed->bEndpointAddress;
		}
	}

	if (intr == -1 || bin == -1 || bout == -1) {
		if (intr == -1) {
			printf("%s: no interrupt end point\n",
			       sc->sc_dev.dv_xname);
		}
		if (bin == -1) {
			printf("%s: no data bulk in end point\n",
			       sc->sc_dev.dv_xname);
		}
		if (bout == -1) {
			printf("%s: no data bulk out end point\n",
			       sc->sc_dev.dv_xname);
		}
		return -1;
	}
	if (isize < UCHCOM_INTR_LEAST) {
		printf("%s: intr pipe is too short", sc->sc_dev.dv_xname);
		return -1;
	}

	DPRINTF(("%s: bulkin=%d, bulkout=%d, intr=%d, isize=%d\n",
		 sc->sc_dev.dv_xname, bin, bout, intr, isize));

	usbd_claim_iface(sc->sc_udev, ifaceno);

	endpoints->ep_intr = intr;
	endpoints->ep_intr_size = isize;
	endpoints->ep_bulkin = bin;
	endpoints->ep_bulkin_size = binsize;
	endpoints->ep_bulkout = bout;

	return 0;
}


/* ----------------------------------------------------------------------
 * low level i/o
 */

usbd_status
uchcom_generic_control_out(struct uchcom_softc *sc, uint8_t reqno,
    uint16_t value, uint16_t index)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = reqno;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, 0);

	return usbd_do_request(sc->sc_udev, &req, 0);
}

usbd_status
uchcom_generic_control_in(struct uchcom_softc *sc, uint8_t reqno,
    uint16_t value, uint16_t index, void *buf, int buflen, int *actlen)
{
	usb_device_request_t req;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = reqno;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, (uint16_t)buflen);

	return usbd_do_request_flags(sc->sc_udev, &req, buf,
				     USBD_SHORT_XFER_OK, actlen,
				     USBD_DEFAULT_TIMEOUT);
}

usbd_status
uchcom_write_reg(struct uchcom_softc *sc,
    uint8_t reg1, uint8_t val1, uint8_t reg2, uint8_t val2)
{
	DPRINTF(("uchcom: write reg 0x%02X<-0x%02X, 0x%02X<-0x%02X\n",
		 (unsigned)reg1, (unsigned)val1,
		 (unsigned)reg2, (unsigned)val2));
	return uchcom_generic_control_out(sc,
	    	(sc->sc_type != UCHCOM_TYPE_CH343) ?
		UCHCOM_REQ_WRITE_REG : UCHCOM_REQ_CH343_WRITE_REG,
		reg1|((uint16_t)reg2<<8), val1|((uint16_t)val2<<8));
}

usbd_status
uchcom_read_reg(struct uchcom_softc *sc,
    uint8_t reg1, uint8_t *rval1, uint8_t reg2, uint8_t *rval2)
{
	uint8_t buf[UCHCOM_INPUT_BUF_SIZE];
	usbd_status err;
	int actin;

	err = uchcom_generic_control_in(
		sc, UCHCOM_REQ_READ_REG,
		reg1|((uint16_t)reg2<<8), 0, buf, sizeof buf, &actin);
	if (err)
		return err;

	DPRINTF(("uchcom: read reg 0x%02X->0x%02X, 0x%02X->0x%02X\n",
		 (unsigned)reg1, (unsigned)buf[0],
		 (unsigned)reg2, (unsigned)buf[1]));

	if (rval1) *rval1 = buf[0];
	if (rval2) *rval2 = buf[1];

	return USBD_NORMAL_COMPLETION;
}

usbd_status
uchcom_get_version(struct uchcom_softc *sc, uint8_t *rver)
{
	uint8_t buf[UCHCOM_INPUT_BUF_SIZE];
	usbd_status err;
	int actin;

	err = uchcom_generic_control_in(
		sc, UCHCOM_REQ_GET_VERSION, 0, 0, buf, sizeof buf, &actin);
	if (err)
		return err;

	if (rver) *rver = buf[0];

	return USBD_NORMAL_COMPLETION;
}

usbd_status
uchcom_read_status(struct uchcom_softc *sc, uint8_t *rval)
{
	return uchcom_read_reg(sc, UCHCOM_REG_STAT1, rval, UCHCOM_REG_STAT2,
	    NULL);
}

usbd_status
uchcom_set_dtrrts_10(struct uchcom_softc *sc, uint8_t val)
{
	return uchcom_write_reg(sc, UCHCOM_REG_STAT1, val, UCHCOM_REG_STAT1,
	    val);
}

usbd_status
uchcom_set_dtrrts_20(struct uchcom_softc *sc, uint8_t val)
{
	return uchcom_generic_control_out(sc, UCHCOM_REQ_SET_DTRRTS, val, 0);
}


/* ----------------------------------------------------------------------
 * middle layer
 */

int
uchcom_update_version(struct uchcom_softc *sc)
{
	usbd_status err;

	err = uchcom_get_version(sc, &sc->sc_version);
	if (err) {
		printf("%s: cannot get version: %s\n",
		       sc->sc_dev.dv_xname, usbd_errstr(err));
		return EIO;
	}

	return 0;
}

void
uchcom_convert_status(struct uchcom_softc *sc, uint8_t cur)
{
	cur = ~cur & 0x0F;
	sc->sc_msr = (cur << 4) | ((sc->sc_msr >> 4) ^ cur);
}

int
uchcom_update_status(struct uchcom_softc *sc)
{
	usbd_status err;
	uint8_t cur;

	err = uchcom_read_status(sc, &cur);
	if (err) {
		printf("%s: cannot update status: %s\n",
		       sc->sc_dev.dv_xname, usbd_errstr(err));
		return EIO;
	}

	sc->sc_dtr = !(cur & UCHCOM_DTR_MASK);
	sc->sc_rts = !(cur & UCHCOM_RTS_MASK);

	uchcom_convert_status(sc, cur);

	return 0;
}


int
uchcom_set_dtrrts(struct uchcom_softc *sc, int dtr, int rts)
{
	usbd_status err;
	uint8_t val = 0;

	if (dtr) val |= UCHCOM_DTR_MASK;
	if (rts) val |= UCHCOM_RTS_MASK;

	if (sc->sc_version < UCHCOM_VER_20)
		err = uchcom_set_dtrrts_10(sc, ~val);
	else
		err = uchcom_set_dtrrts_20(sc, ~val);

	if (err) {
		printf("%s: cannot set DTR/RTS: %s\n",
		       sc->sc_dev.dv_xname, usbd_errstr(err));
		return EIO;
	}

	return 0;
}

int
uchcom_set_break(struct uchcom_softc *sc, int onoff)
{
	usbd_status err;
	uint8_t brk, lcr;

	err = uchcom_read_reg(sc, UCHCOM_REG_BREAK, &brk, UCHCOM_REG_LCR, &lcr);
	if (err)
		return EIO;
	if (onoff) {
		/* on - clear bits */
		brk &= ~UCHCOM_BREAK_MASK;
		lcr &= ~UCHCOM_LCR_TXE;
	} else {
		/* off - set bits */
		brk |= UCHCOM_BREAK_MASK;
		lcr |= UCHCOM_LCR_TXE;
	}
	err = uchcom_write_reg(sc, UCHCOM_REG_BREAK, brk, UCHCOM_REG_LCR, lcr);
	if (err)
		return EIO;

	return 0;
}

int
uchcom_set_break_ch343(struct uchcom_softc *sc, int onoff)
{
	usbd_status err;
	uint8_t brk = UCHCOM_CH343_BREAK_MASK;

	if (!onoff)
		brk |= UCHCOM_ABREAK_MASK;

	err = uchcom_write_reg(sc, brk, 0, 0, 0);
	if (err)
		return EIO;

	return 0;
}

void
uchcom_calc_baudrate(struct uchcom_softc *sc, uint32_t rate, uint8_t *divisor,
    uint8_t *factor)
{
	uint32_t clk = 12000000;

	if (rate == 921600 || rate == 4000000)
		*divisor = 7;
	else if (rate > 23529) {
		clk /= 2;
		*divisor = 3;
	} else if (rate > 2941) {
		clk /=  16;
		*divisor = 2;
	} else if (rate > 367) {
		clk /= 128;
		*divisor = 1;
	} else {
		clk = 11719;
		*divisor = 0;
	}

	if (rate == 921600 && sc->sc_type != UCHCOM_TYPE_CH343)
		*factor = 243;
	else
		*factor = 256 - clk / rate;
}

int
uchcom_set_dte_rate(struct uchcom_softc *sc, uint32_t rate, uint16_t val)
{
	usbd_status err;
	uint16_t idx;
	uint8_t factor, div;

	uchcom_calc_baudrate(sc, rate, &div, &factor);
	div |= (sc->sc_type != UCHCOM_TYPE_CH343) ? 0x80 : 0;
	idx = (factor << 8) | div;

	err = uchcom_generic_control_out(sc, UCHCOM_REQ_SET_BAUDRATE, val, idx);
	if (err) {
		printf("%s: cannot set DTE rate: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return EIO;
	}

	return 0;
}

uint16_t
uchcom_set_line_control(struct uchcom_softc *sc, tcflag_t cflag, uint16_t *val)
{
	uint8_t lcr = 0;

	if (sc->sc_release == UCHCOM_REV_CH340) {
		/*
		 * XXX: it is difficult to handle the line control
		 * appropriately on CH340:
		 *   work as chip default - CS8, no parity, !CSTOPB
		 *   other modes are not supported.
		 */
		switch (ISSET(cflag, CSIZE)) {
		case CS5:
		case CS6:
		case CS7:
			return EINVAL;
		case CS8:
			break;
		}
		if (ISSET(cflag, PARENB) || ISSET(cflag, CSTOPB))
			return EINVAL;
		return 0;
	}

	lcr = UCHCOM_LCR_RXE | UCHCOM_LCR_TXE;

	switch (ISSET(cflag, CSIZE)) {
	case CS5:
		lcr |= UCHCOM_LCR_CS5;
		break;
	case CS6:
		lcr |= UCHCOM_LCR_CS6;
		break;
	case CS7:
		lcr |= UCHCOM_LCR_CS7;
		break;
	case CS8:
		lcr |= UCHCOM_LCR_CS8;
		break;
	}

	if (ISSET(cflag, PARENB)) {
		lcr |= UCHCOM_LCR_PARENB;
		if (!ISSET(cflag, PARODD))
			lcr |= UCHCOM_LCR_PAREVEN;
	}

	if (ISSET(cflag, CSTOPB)) {
		lcr |= UCHCOM_LCR_STOPB;
	}

	*val = UCHCOM_T | UCHCOM_CL | UCHCOM_CT | lcr << 8;
	*val |= (sc->sc_type != UCHCOM_TYPE_CH343) ? 0x10 : 0;

	return 0;
}

int
uchcom_clear_chip(struct uchcom_softc *sc)
{
	usbd_status err;

	DPRINTF(("%s: clear\n", sc->sc_dev.dv_xname));
	err = uchcom_generic_control_out(sc, UCHCOM_REQ_RESET, 0, 0);
	if (err) {
		printf("%s: cannot clear: %s\n",
		       sc->sc_dev.dv_xname, usbd_errstr(err));
		return EIO;
	}

	return 0;
}

int
uchcom_setup_intr_pipe(struct uchcom_softc *sc)
{
	usbd_status err;

	if (sc->sc_intr_endpoint != -1 && sc->sc_intr_pipe == NULL) {
		sc->sc_intr_buf = malloc(sc->sc_isize, M_USBDEV, M_WAITOK);
		err = usbd_open_pipe_intr(sc->sc_intr_iface,
					  sc->sc_intr_endpoint,
					  USBD_SHORT_XFER_OK,
					  &sc->sc_intr_pipe, sc,
					  sc->sc_intr_buf,
					  sc->sc_isize,
					  uchcom_intr, USBD_DEFAULT_INTERVAL);
		if (err) {
			printf("%s: cannot open interrupt pipe: %s\n",
			       sc->sc_dev.dv_xname,
			       usbd_errstr(err));
			return EIO;
		}
	}
	return 0;
}

void
uchcom_close_intr_pipe(struct uchcom_softc *sc)
{
	usbd_status err;

	if (sc->sc_intr_pipe != NULL) {
		err = usbd_close_pipe(sc->sc_intr_pipe);
		if (err)
			printf("%s: close interrupt pipe failed: %s\n",
			       sc->sc_dev.dv_xname, usbd_errstr(err));
		free(sc->sc_intr_buf, M_USBDEV, sc->sc_isize);
		sc->sc_intr_pipe = NULL;
	}
}


/* ----------------------------------------------------------------------
 * methods for ucom
 */
void
uchcom_get_status(void *arg, int portno, u_char *rlsr, u_char *rmsr)
{
	struct uchcom_softc *sc = arg;

	if (usbd_is_dying(sc->sc_udev))
		return;

	*rlsr = sc->sc_lsr;
	*rmsr = sc->sc_msr;
}

void
uchcom_set(void *arg, int portno, int reg, int onoff)
{
	struct uchcom_softc *sc = arg;

	if (usbd_is_dying(sc->sc_udev))
		return;

	switch (reg) {
	case UCOM_SET_DTR:
		sc->sc_dtr = !!onoff;
		uchcom_set_dtrrts(sc, sc->sc_dtr, sc->sc_rts);
		break;
	case UCOM_SET_RTS:
		sc->sc_rts = !!onoff;
		uchcom_set_dtrrts(sc, sc->sc_dtr, sc->sc_rts);
		break;
	case UCOM_SET_BREAK:
		if (sc->sc_type == UCHCOM_TYPE_CH343)
			uchcom_set_break_ch343(sc, onoff);
		else
			uchcom_set_break(sc, onoff);
		break;
	}
}

int
uchcom_param(void *arg, int portno, struct termios *t)
{
	struct uchcom_softc *sc = arg;
	uint16_t val = 0;
	int ret;

	if (usbd_is_dying(sc->sc_udev))
		return 0;

	if (t->c_ospeed <= 0 ||
	    (t->c_ospeed > 921600 && sc->sc_type != UCHCOM_TYPE_CH343) ||
	    (t->c_ospeed > 6000000 && sc->sc_type == UCHCOM_TYPE_CH343))
		return EINVAL;

	ret = uchcom_set_line_control(sc, t->c_cflag, &val);
	if (ret)
		return ret;

	ret = uchcom_set_dte_rate(sc, t->c_ospeed, val);
	if (ret)
		return ret;

	return 0;
}

int
uchcom_open(void *arg, int portno)
{
	struct uchcom_softc *sc = arg;
	int ret;

	if (usbd_is_dying(sc->sc_udev))
		return EIO;

	ret = uchcom_setup_intr_pipe(sc);
	if (ret)
		return ret;

	ret = uchcom_update_version(sc);
	if (ret)
		return ret;

	if (sc->sc_type != UCHCOM_TYPE_CH343) {
		ret = uchcom_clear_chip(sc);
		if (ret)
			return ret;
	}

	ret = uchcom_update_status(sc);
	if (ret)
		return ret;

	sc->sc_dtr = sc->sc_rts = 0;
	ret = uchcom_set_dtrrts(sc, sc->sc_dtr, sc->sc_rts);
	if (ret)
		return ret;

	return 0;
}

void
uchcom_close(void *arg, int portno)
{
	struct uchcom_softc *sc = arg;

	uchcom_close_intr_pipe(sc);
}


/* ----------------------------------------------------------------------
 * callback when the modem status is changed.
 */
void
uchcom_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct uchcom_softc *sc = priv;
	u_char *buf = sc->sc_intr_buf;
	uint32_t intrstat;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		DPRINTF(("%s: abnormal status: %s\n",
			 sc->sc_dev.dv_xname, usbd_errstr(status)));
		usbd_clear_endpoint_stall_async(sc->sc_intr_pipe);
		return;
	}
	DPRINTF(("%s: intr: 0x%02X 0x%02X 0x%02X 0x%02X "
		 "0x%02X 0x%02X 0x%02X 0x%02X\n",
		 sc->sc_dev.dv_xname,
		 (unsigned)buf[0], (unsigned)buf[1],
		 (unsigned)buf[2], (unsigned)buf[3],
		 (unsigned)buf[4], (unsigned)buf[5],
		 (unsigned)buf[6], (unsigned)buf[7]));

	intrstat = (sc->sc_type == UCHCOM_TYPE_CH343) ?
	    xfer->actlen - 1 : UCHCOM_INTR_STAT1;

	uchcom_convert_status(sc, buf[intrstat]);
	ucom_status_change((struct ucom_softc *) sc->sc_subdev);
}
