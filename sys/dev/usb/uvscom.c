/*	$OpenBSD: uvscom.c,v 1.43 2024/05/23 03:21:09 jsg Exp $ */
/*	$NetBSD: uvscom.c,v 1.9 2003/02/12 15:36:20 ichiro Exp $	*/
/*-
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
 *
 * $FreeBSD: src/sys/dev/usb/uvscom.c,v 1.1 2002/03/18 18:23:39 joe Exp $
 */

/*
 * uvscom: SUNTAC Slipper U VS-10U driver.
 * Slipper U is a PC card to USB converter for data communication card
 * adapter.  It supports DDI Pocket's Air H" C@rd, C@rd H" 64, NTT's P-in,
 * P-in m@ater and various data communication card adapters.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/device.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/ucomvar.h>

#ifdef UVSCOM_DEBUG
static int	uvscomdebug = 1;

#define DPRINTFN(n, x)  do { if (uvscomdebug > (n)) printf x; } while (0)
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x) DPRINTFN(0, x)

#define	UVSCOM_IFACE_INDEX	0

#define UVSCOM_INTR_INTERVAL	100	/* mS */

#define UVSCOM_UNIT_WAIT	5

/* Request */
#define UVSCOM_SET_SPEED	0x10
#define UVSCOM_LINE_CTL		0x11
#define UVSCOM_SET_PARAM	0x12
#define UVSCOM_READ_STATUS	0xd0
#define UVSCOM_SHUTDOWN		0xe0

/* UVSCOM_SET_SPEED parameters */
#define UVSCOM_SPEED_150BPS	0x00
#define UVSCOM_SPEED_300BPS	0x01
#define UVSCOM_SPEED_600BPS	0x02
#define UVSCOM_SPEED_1200BPS	0x03
#define UVSCOM_SPEED_2400BPS	0x04
#define UVSCOM_SPEED_4800BPS	0x05
#define UVSCOM_SPEED_9600BPS	0x06
#define UVSCOM_SPEED_19200BPS	0x07
#define UVSCOM_SPEED_38400BPS	0x08
#define UVSCOM_SPEED_57600BPS	0x09
#define UVSCOM_SPEED_115200BPS	0x0a

/* UVSCOM_LINE_CTL parameters */
#define UVSCOM_BREAK		0x40
#define UVSCOM_RTS		0x02
#define UVSCOM_DTR		0x01
#define UVSCOM_LINE_INIT	0x08

/* UVSCOM_SET_PARAM parameters */
#define UVSCOM_DATA_MASK	0x03
#define UVSCOM_DATA_BIT_8	0x03
#define UVSCOM_DATA_BIT_7	0x02
#define UVSCOM_DATA_BIT_6	0x01
#define UVSCOM_DATA_BIT_5	0x00

#define UVSCOM_STOP_MASK	0x04
#define UVSCOM_STOP_BIT_2	0x04
#define UVSCOM_STOP_BIT_1	0x00

#define UVSCOM_PARITY_MASK	0x18
#define UVSCOM_PARITY_EVEN	0x18
#if 0
#define UVSCOM_PARITY_UNK	0x10
#endif
#define UVSCOM_PARITY_ODD	0x08
#define UVSCOM_PARITY_NONE	0x00

/* Status bits */
#define UVSCOM_TXRDY		0x04
#define UVSCOM_RXRDY		0x01

#define UVSCOM_DCD		0x08
#define UVSCOM_NOCARD		0x04
#define UVSCOM_DSR		0x02
#define UVSCOM_CTS		0x01
#define UVSCOM_USTAT_MASK	(UVSCOM_NOCARD | UVSCOM_DSR | UVSCOM_CTS)

struct	uvscom_softc {
	struct device		sc_dev;		/* base device */
	struct usbd_device	*sc_udev;	/* USB device */
	struct usbd_interface	*sc_iface;	/* interface */

	struct usbd_interface	*sc_intr_iface;	/* interrupt interface */
	int			sc_intr_number;	/* interrupt number */
	struct usbd_pipe	*sc_intr_pipe;	/* interrupt pipe */
	u_char			*sc_intr_buf;	/* interrupt buffer */
	int			sc_isize;

	u_char			sc_dtr;		/* current DTR state */
	u_char			sc_rts;		/* current RTS state */

	u_char			sc_lsr;		/* Local status register */
	u_char			sc_msr;		/* uvscom status register */

	uint16_t		sc_lcr;		/* Line control */
	u_char			sc_usr;		/* unit status */

	struct device		*sc_subdev;	/* ucom device */
};

/*
 * These are the maximum number of bytes transferred per frame.
 * The output buffer size cannot be increased due to the size encoding.
 */
#define UVSCOMIBUFSIZE 512
#define UVSCOMOBUFSIZE 64

usbd_status uvscom_readstat(struct uvscom_softc *);
usbd_status uvscom_shutdown(struct uvscom_softc *);
usbd_status uvscom_reset(struct uvscom_softc *);
usbd_status uvscom_set_line_coding(struct uvscom_softc *,
					   uint16_t, uint16_t);
usbd_status uvscom_set_line(struct uvscom_softc *, uint16_t);
usbd_status uvscom_set_crtscts(struct uvscom_softc *);
void uvscom_get_status(void *, int, u_char *, u_char *);
void uvscom_dtr(struct uvscom_softc *, int);
void uvscom_rts(struct uvscom_softc *, int);
void uvscom_break(struct uvscom_softc *, int);

void uvscom_set(void *, int, int, int);
void uvscom_intr(struct usbd_xfer *, void *, usbd_status);
int  uvscom_param(void *, int, struct termios *);
int  uvscom_open(void *, int);
void uvscom_close(void *, int);

const struct ucom_methods uvscom_methods = {
	uvscom_get_status,
	uvscom_set,
	uvscom_param,
	NULL, /* uvscom_ioctl, TODO */
	uvscom_open,
	uvscom_close,
	NULL,
	NULL
};

static const struct usb_devno uvscom_devs [] = {
	/* SUNTAC U-Cable type A3 */
	{ USB_VENDOR_SUNTAC, USB_PRODUCT_SUNTAC_AS64LX },
	/* SUNTAC U-Cable type A4 */
	{ USB_VENDOR_SUNTAC, USB_PRODUCT_SUNTAC_AS144L4 },
	/* SUNTAC U-Cable type D2 */
	{ USB_VENDOR_SUNTAC, USB_PRODUCT_SUNTAC_DS96L },
	/* SUNTAC U-Cable type P1 */
	{ USB_VENDOR_SUNTAC, USB_PRODUCT_SUNTAC_PS64P1 },
	/* SUNTAC Slipper U  */
	{ USB_VENDOR_SUNTAC, USB_PRODUCT_SUNTAC_VS10U },
	/* SUNTAC Ir-Trinity */
	{ USB_VENDOR_SUNTAC, USB_PRODUCT_SUNTAC_IS96U },
};

int uvscom_match(struct device *, void *, void *);
void uvscom_attach(struct device *, struct device *, void *);
int uvscom_detach(struct device *, int);

struct cfdriver uvscom_cd = {
	NULL, "uvscom", DV_DULL
};

const struct cfattach uvscom_ca = {
	sizeof(struct uvscom_softc), uvscom_match, uvscom_attach, uvscom_detach
};

int
uvscom_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	return (usb_lookup(uvscom_devs, uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
uvscom_attach(struct device *parent, struct device *self, void *aux)
{
	struct uvscom_softc *sc = (struct uvscom_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_device *dev = uaa->device;
	usb_config_descriptor_t *cdesc;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	const char *devname = sc->sc_dev.dv_xname;
	usbd_status err;
	int i;
	struct ucom_attach_args uca;

        sc->sc_udev = dev;

	DPRINTF(("uvscom attach: sc = %p\n", sc));

	/* initialize endpoints */
	uca.bulkin = uca.bulkout = -1;
	sc->sc_intr_number = -1;
	sc->sc_intr_pipe = NULL;

	/* get the config descriptor */
	cdesc = usbd_get_config_descriptor(sc->sc_udev);

	if (cdesc == NULL) {
		printf("%s: failed to get configuration descriptor\n",
			sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	/* get the common interface */
	err = usbd_device2interface_handle(dev, UVSCOM_IFACE_INDEX,
					   &sc->sc_iface);
	if (err) {
		printf("%s: failed to get interface, err=%s\n",
			devname, usbd_errstr(err));
		usbd_deactivate(sc->sc_udev);
		return;
	}

	id = usbd_get_interface_descriptor(sc->sc_iface);

	/* Find endpoints */
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor for %d\n",
				sc->sc_dev.dv_xname, i);
			usbd_deactivate(sc->sc_udev);
			return;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			uca.bulkin = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			uca.bulkout = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->sc_intr_number = ed->bEndpointAddress;
			sc->sc_isize = UGETW(ed->wMaxPacketSize);
		}
	}

	if (uca.bulkin == -1) {
		printf("%s: Could not find data bulk in\n",
			sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}
	if (uca.bulkout == -1) {
		printf("%s: Could not find data bulk out\n",
			sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}
	if (sc->sc_intr_number == -1) {
		printf("%s: Could not find interrupt in\n",
			sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	sc->sc_dtr = sc->sc_rts = 0;
	sc->sc_lcr = UVSCOM_LINE_INIT;

	uca.portno = UCOM_UNK_PORTNO;
	/* bulkin, bulkout set above */
	uca.ibufsize = UVSCOMIBUFSIZE;
	uca.obufsize = UVSCOMOBUFSIZE;
	uca.ibufsizepad = UVSCOMIBUFSIZE;
	uca.opkthdrlen = 0;
	uca.device = dev;
	uca.iface = sc->sc_iface;
	uca.methods = &uvscom_methods;
	uca.arg = sc;
	uca.info = NULL;

	err = uvscom_reset(sc);

	if (err) {
		printf("%s: reset failed, %s\n", sc->sc_dev.dv_xname,
			usbd_errstr(err));
		usbd_deactivate(sc->sc_udev);
		return;
	}

	DPRINTF(("uvscom: in = 0x%x out = 0x%x intr = 0x%x\n",
		 ucom->sc_bulkin_no, ucom->sc_bulkout_no, sc->sc_intr_number));

	DPRINTF(("uplcom: in=0x%x out=0x%x intr=0x%x\n",
			uca.bulkin, uca.bulkout, sc->sc_intr_number ));
	sc->sc_subdev = config_found_sm(self, &uca, ucomprint, ucomsubmatch);
}

int
uvscom_detach(struct device *self, int flags)
{
	struct uvscom_softc *sc = (struct uvscom_softc *)self;
	int rv = 0;

	DPRINTF(("uvscom_detach: sc = %p\n", sc));

	if (sc->sc_intr_pipe != NULL) {
		usbd_close_pipe(sc->sc_intr_pipe);
		free(sc->sc_intr_buf, M_USBDEV, sc->sc_isize);
		sc->sc_intr_pipe = NULL;
	}

	if (sc->sc_subdev != NULL) {
		rv = config_detach(sc->sc_subdev, flags);
		sc->sc_subdev = NULL;
	}

	return (rv);
}

usbd_status
uvscom_readstat(struct uvscom_softc *sc)
{
	usb_device_request_t req;
	usbd_status err;
	uint16_t r;

	DPRINTF(("%s: send readstat\n", sc->sc_dev.dv_xname));

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = UVSCOM_READ_STATUS;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 2);

	err = usbd_do_request(sc->sc_udev, &req, &r);
	if (err) {
		printf("%s: uvscom_readstat: %s\n",
		       sc->sc_dev.dv_xname, usbd_errstr(err));
		return (err);
	}

	DPRINTF(("%s: uvscom_readstat: r = %d\n",
		 sc->sc_dev.dv_xname, r));

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvscom_shutdown(struct uvscom_softc *sc)
{
	usb_device_request_t req;
	usbd_status err;

	DPRINTF(("%s: send shutdown\n", sc->sc_dev.dv_xname));

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UVSCOM_SHUTDOWN;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err) {
		printf("%s: uvscom_shutdown: %s\n",
		       sc->sc_dev.dv_xname, usbd_errstr(err));
		return (err);
	}

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvscom_reset(struct uvscom_softc *sc)
{
	DPRINTF(("%s: uvscom_reset\n", sc->sc_dev.dv_xname));

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvscom_set_crtscts(struct uvscom_softc *sc)
{
	DPRINTF(("%s: uvscom_set_crtscts\n", sc->sc_dev.dv_xname));

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvscom_set_line(struct uvscom_softc *sc, uint16_t line)
{
	usb_device_request_t req;
	usbd_status err;

	DPRINTF(("%s: uvscom_set_line: %04x\n",
		 sc->sc_dev.dv_xname, line));

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UVSCOM_LINE_CTL;
	USETW(req.wValue, line);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err) {
		printf("%s: uvscom_set_line: %s\n",
		       sc->sc_dev.dv_xname, usbd_errstr(err));
		return (err);
	}

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uvscom_set_line_coding(struct uvscom_softc *sc, uint16_t lsp, uint16_t ls)
{
	usb_device_request_t req;
	usbd_status err;

	DPRINTF(("%s: uvscom_set_line_coding: %02x %02x\n",
		 sc->sc_dev.dv_xname, lsp, ls));

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UVSCOM_SET_SPEED;
	USETW(req.wValue, lsp);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err) {
		printf("%s: uvscom_set_line_coding: %s\n",
		       sc->sc_dev.dv_xname, usbd_errstr(err));
		return (err);
	}

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UVSCOM_SET_PARAM;
	USETW(req.wValue, ls);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err) {
		printf("%s: uvscom_set_line_coding: %s\n",
		       sc->sc_dev.dv_xname, usbd_errstr(err));
		return (err);
	}

	return (USBD_NORMAL_COMPLETION);
}

void
uvscom_dtr(struct uvscom_softc *sc, int onoff)
{
	DPRINTF(("%s: uvscom_dtr: onoff = %d\n",
		 sc->sc_dev.dv_xname, onoff));

	if (sc->sc_dtr == onoff)
		return;			/* no change */

	sc->sc_dtr = onoff;

	if (onoff)
		SET(sc->sc_lcr, UVSCOM_DTR);
	else
		CLR(sc->sc_lcr, UVSCOM_DTR);

	uvscom_set_line(sc, sc->sc_lcr);
}

void
uvscom_rts(struct uvscom_softc *sc, int onoff)
{
	DPRINTF(("%s: uvscom_rts: onoff = %d\n",
		 sc->sc_dev.dv_xname, onoff));

	if (sc->sc_rts == onoff)
		return;			/* no change */

	sc->sc_rts = onoff;

	if (onoff)
		SET(sc->sc_lcr, UVSCOM_RTS);
	else
		CLR(sc->sc_lcr, UVSCOM_RTS);

	uvscom_set_line(sc, sc->sc_lcr);
}

void
uvscom_break(struct uvscom_softc *sc, int onoff)
{
	DPRINTF(("%s: uvscom_break: onoff = %d\n",
		 sc->sc_dev.dv_xname, onoff));

	if (onoff)
		uvscom_set_line(sc, SET(sc->sc_lcr, UVSCOM_BREAK));
}

void
uvscom_set(void *addr, int portno, int reg, int onoff)
{
	struct uvscom_softc *sc = addr;

	switch (reg) {
	case UCOM_SET_DTR:
		uvscom_dtr(sc, onoff);
		break;
	case UCOM_SET_RTS:
		uvscom_rts(sc, onoff);
		break;
	case UCOM_SET_BREAK:
		uvscom_break(sc, onoff);
		break;
	default:
		break;
	}
}

int
uvscom_param(void *addr, int portno, struct termios *t)
{
	struct uvscom_softc *sc = addr;
	usbd_status err;
	uint16_t lsp;
	uint16_t ls;

	DPRINTF(("%s: uvscom_param: sc = %p\n",
		 sc->sc_dev.dv_xname, sc));

	ls = 0;

	switch (t->c_ospeed) {
	case B150:
		lsp = UVSCOM_SPEED_150BPS;
		break;
	case B300:
		lsp = UVSCOM_SPEED_300BPS;
		break;
	case B600:
		lsp = UVSCOM_SPEED_600BPS;
		break;
	case B1200:
		lsp = UVSCOM_SPEED_1200BPS;
		break;
	case B2400:
		lsp = UVSCOM_SPEED_2400BPS;
		break;
	case B4800:
		lsp = UVSCOM_SPEED_4800BPS;
		break;
	case B9600:
		lsp = UVSCOM_SPEED_9600BPS;
		break;
	case B19200:
		lsp = UVSCOM_SPEED_19200BPS;
		break;
	case B38400:
		lsp = UVSCOM_SPEED_38400BPS;
		break;
	case B57600:
		lsp = UVSCOM_SPEED_57600BPS;
		break;
	case B115200:
		lsp = UVSCOM_SPEED_115200BPS;
		break;
	default:
		return (EIO);
	}

	if (ISSET(t->c_cflag, CSTOPB))
		SET(ls, UVSCOM_STOP_BIT_2);
	else
		SET(ls, UVSCOM_STOP_BIT_1);

	if (ISSET(t->c_cflag, PARENB)) {
		if (ISSET(t->c_cflag, PARODD))
			SET(ls, UVSCOM_PARITY_ODD);
		else
			SET(ls, UVSCOM_PARITY_EVEN);
	} else
		SET(ls, UVSCOM_PARITY_NONE);

	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
		SET(ls, UVSCOM_DATA_BIT_5);
		break;
	case CS6:
		SET(ls, UVSCOM_DATA_BIT_6);
		break;
	case CS7:
		SET(ls, UVSCOM_DATA_BIT_7);
		break;
	case CS8:
		SET(ls, UVSCOM_DATA_BIT_8);
		break;
	default:
		return (EIO);
	}

	err = uvscom_set_line_coding(sc, lsp, ls);
	if (err)
		return (EIO);

	if (ISSET(t->c_cflag, CRTSCTS)) {
		err = uvscom_set_crtscts(sc);
		if (err)
			return (EIO);
	}

	return (0);
}

int
uvscom_open(void *addr, int portno)
{
	struct uvscom_softc *sc = addr;
	int err;
	int i;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	DPRINTF(("uvscom_open: sc = %p\n", sc));

	if (sc->sc_intr_number != -1 && sc->sc_intr_pipe == NULL) {
		DPRINTF(("uvscom_open: open interrupt pipe.\n"));

		sc->sc_usr = 0;		/* clear unit status */

		err = uvscom_readstat(sc);
		if (err) {
			DPRINTF(("%s: uvscom_open: readstat failed\n",
				 sc->sc_dev.dv_xname));
			return (EIO);
		}

		sc->sc_intr_buf = malloc(sc->sc_isize, M_USBDEV, M_WAITOK);
		err = usbd_open_pipe_intr(sc->sc_iface,
					  sc->sc_intr_number,
					  USBD_SHORT_XFER_OK,
					  &sc->sc_intr_pipe,
					  sc,
					  sc->sc_intr_buf,
					  sc->sc_isize,
					  uvscom_intr,
					  UVSCOM_INTR_INTERVAL);
		if (err) {
			printf("%s: cannot open interrupt pipe (addr %d)\n",
				 sc->sc_dev.dv_xname,
				 sc->sc_intr_number);
			return (EIO);
		}
	} else {
		DPRINTF(("uvscom_open: did not open interrupt pipe.\n"));
	}

	if ((sc->sc_usr & UVSCOM_USTAT_MASK) == 0) {
		/* unit is not ready */

		for (i = UVSCOM_UNIT_WAIT; i > 0; --i) {
			tsleep_nsec(&err, TTIPRI, "uvsop", SEC_TO_NSEC(1));
			if (ISSET(sc->sc_usr, UVSCOM_USTAT_MASK))
				break;
		}
		if (i == 0) {
			DPRINTF(("%s: unit is not ready\n",
				 sc->sc_dev.dv_xname));
			return (EIO);
		}

		/* check PC card was inserted */
		if (ISSET(sc->sc_usr, UVSCOM_NOCARD)) {
			DPRINTF(("%s: no card\n",
				 sc->sc_dev.dv_xname));
			return (EIO);
		}
	}

	return (0);
}

void
uvscom_close(void *addr, int portno)
{
	struct uvscom_softc *sc = addr;
	int err;

	if (usbd_is_dying(sc->sc_udev))
		return;

	DPRINTF(("uvscom_close: close\n"));

	uvscom_shutdown(sc);

	if (sc->sc_intr_pipe != NULL) {
		err = usbd_close_pipe(sc->sc_intr_pipe);
		if (err)
			printf("%s: close interrupt pipe failed: %s\n",
				sc->sc_dev.dv_xname,
					   usbd_errstr(err));
		free(sc->sc_intr_buf, M_USBDEV, sc->sc_isize);
		sc->sc_intr_pipe = NULL;
	}
}

void
uvscom_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct uvscom_softc *sc = priv;
	u_char *buf = sc->sc_intr_buf;
	u_char pstatus;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		printf("%s: uvscom_intr: abnormal status: %s\n",
			sc->sc_dev.dv_xname,
			usbd_errstr(status));
		usbd_clear_endpoint_stall_async(sc->sc_intr_pipe);
		return;
	}

	DPRINTFN(2, ("%s: uvscom status = %02x %02x\n",
		 sc->sc_dev.dv_xname, buf[0], buf[1]));

	sc->sc_lsr = sc->sc_msr = 0;
	sc->sc_usr = buf[1];

	pstatus = buf[0];
	if (ISSET(pstatus, UVSCOM_TXRDY))
		SET(sc->sc_lsr, ULSR_TXRDY);
	if (ISSET(pstatus, UVSCOM_RXRDY))
		SET(sc->sc_lsr, ULSR_RXRDY);

	pstatus = buf[1];
	if (ISSET(pstatus, UVSCOM_CTS))
		SET(sc->sc_msr, UMSR_CTS);
	if (ISSET(pstatus, UVSCOM_DSR))
		SET(sc->sc_msr, UMSR_DSR);
	if (ISSET(pstatus, UVSCOM_DCD))
		SET(sc->sc_msr, UMSR_DCD);

	ucom_status_change((struct ucom_softc *) sc->sc_subdev);
}

void
uvscom_get_status(void *addr, int portno, u_char *lsr, u_char *msr)
{
	struct uvscom_softc *sc = addr;

	if (lsr != NULL)
		*lsr = sc->sc_lsr;
	if (msr != NULL)
		*msr = sc->sc_msr;
}
