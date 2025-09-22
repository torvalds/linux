/*	$OpenBSD: umct.c,v 1.51 2024/05/23 03:21:09 jsg Exp $	*/
/*	$NetBSD: umct.c,v 1.10 2003/02/23 04:20:07 simonb Exp $	*/
/*
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

/*
 * MCT USB-RS232 Interface Controller
 * http://www.mct.com.tw/prod/rs232.html
 * http://www.dlink.com/products/usb/dsbs25
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/device.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbcdc.h>

#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/ucomvar.h>

#include <dev/usb/umct.h>

#ifdef UMCT_DEBUG
#define DPRINTFN(n, x)  do { if (umctdebug > (n)) printf x; } while (0)
int	umctdebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x) DPRINTFN(0, x)

#define	UMCT_IFACE_INDEX	0

struct	umct_softc {
	struct device		 sc_dev;	/* base device */
	struct usbd_device	*sc_udev;	/* USB device */
	struct usbd_interface	*sc_iface;	/* interface */
	int			 sc_iface_number;	/* interface number */
	u_int16_t		 sc_product;

	int			 sc_intr_number;	/* interrupt number */
	struct usbd_pipe	*sc_intr_pipe;	/* interrupt pipe */
	u_char			*sc_intr_buf;	/* interrupt buffer */
	int			 sc_isize;

	struct usb_cdc_line_state sc_line_state;	/* current line state */
	u_char			 sc_dtr;	/* current DTR state */
	u_char			 sc_rts;	/* current RTS state */
	u_char			 sc_break;	/* set break */

	u_char			 sc_status;

	struct device		*sc_subdev;	/* ucom device */

	u_char			 sc_lsr;	/* Local status register */
	u_char			 sc_msr;	/* umct status register */

	u_int			 last_lcr;	/* keep lcr register */
};

/*
 * These are the maximum number of bytes transferred per frame.
 * The output buffer size cannot be increased due to the size encoding.
 */
#define UMCTIBUFSIZE 256
#define UMCTOBUFSIZE 256

void umct_init(struct umct_softc *);
void umct_set_baudrate(struct umct_softc *, u_int, int);
void umct_set_lcr(struct umct_softc *, u_int);
void umct_intr(struct usbd_xfer *, void *, usbd_status);

void umct_set(void *, int, int, int);
void umct_dtr(struct umct_softc *, int);
void umct_rts(struct umct_softc *, int);
void umct_break(struct umct_softc *, int);
void umct_set_line_state(struct umct_softc *);
void umct_get_status(void *, int portno, u_char *lsr, u_char *msr);
int  umct_param(void *, int, struct termios *);
int  umct_open(void *, int);
void umct_close(void *, int);

const struct ucom_methods umct_methods = {
	umct_get_status,
	umct_set,
	umct_param,
	NULL,
	umct_open,
	umct_close,
	NULL,
	NULL,
};

static const struct usb_devno umct_devs[] = {
	/* MCT USB-232 Interface Products */
	{ USB_VENDOR_MCT, USB_PRODUCT_MCT_USB232 },
	/* Sitecom USB-232 Products */
	{ USB_VENDOR_MCT, USB_PRODUCT_MCT_SITECOM_USB232 },
	/* D-Link DU-H3SP USB BAY Hub Products */
	{ USB_VENDOR_MCT, USB_PRODUCT_MCT_DU_H3SP_USB232 },
	/* BELKIN F5U109 */
	{ USB_VENDOR_BELKIN, USB_PRODUCT_BELKIN_F5U109 },
	/* BELKIN F5U409 */
	{ USB_VENDOR_BELKIN, USB_PRODUCT_BELKIN_F5U409 },
};

int umct_match(struct device *, void *, void *);
void umct_attach(struct device *, struct device *, void *);
int umct_detach(struct device *, int);

struct cfdriver umct_cd = {
	NULL, "umct", DV_DULL
};

const struct cfattach umct_ca = {
	sizeof(struct umct_softc), umct_match, umct_attach, umct_detach
};

int
umct_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	return (usb_lookup(umct_devs, uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
umct_attach(struct device *parent, struct device *self, void *aux)
{
	struct umct_softc *sc = (struct umct_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_device *dev = uaa->device;
	usb_config_descriptor_t *cdesc;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;

	char *devname = sc->sc_dev.dv_xname;
	usbd_status err;
	int i;
	struct ucom_attach_args uca;

        sc->sc_udev = dev;
	sc->sc_product = uaa->product;

	DPRINTF(("\n\numct attach: sc=%p\n", sc));

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

	/* get the interface */
	err = usbd_device2interface_handle(dev, UMCT_IFACE_INDEX,
							&sc->sc_iface);
	if (err) {
		printf("\n%s: failed to get interface, err=%s\n",
			devname, usbd_errstr(err));
		usbd_deactivate(sc->sc_udev);
		return;
	}

	/* Find the bulk{in,out} and interrupt endpoints */

	id = usbd_get_interface_descriptor(sc->sc_iface);
	sc->sc_iface_number = id->bInterfaceNumber;

	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor for %d\n",
				sc->sc_dev.dv_xname, i);
			usbd_deactivate(sc->sc_udev);
			return;
		}

		/*
		 * The Bulkin endpoint is marked as an interrupt. Since
		 * we can't rely on the endpoint descriptor order, we'll
		 * check the wMaxPacketSize field to differentiate.
		 */
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT &&
		    UGETW(ed->wMaxPacketSize) != 0x2) {
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

	if (sc->sc_intr_number== -1) {
		printf("%s: Could not find interrupt in\n",
			sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	sc->sc_dtr = sc->sc_rts = 0;
	uca.portno = UCOM_UNK_PORTNO;
	/* bulkin, bulkout set above */
	uca.ibufsize = UMCTIBUFSIZE;
	if (sc->sc_product == USB_PRODUCT_MCT_SITECOM_USB232)
		uca.obufsize = 16; /* device is broken */
	else
		uca.obufsize = UMCTOBUFSIZE;
	uca.ibufsizepad = UMCTIBUFSIZE;
	uca.opkthdrlen = 0;
	uca.device = dev;
	uca.iface = sc->sc_iface;
	uca.methods = &umct_methods;
	uca.arg = sc;
	uca.info = NULL;

	umct_init(sc);

	DPRINTF(("umct: in=0x%x out=0x%x intr=0x%x\n",
			uca.bulkin, uca.bulkout, sc->sc_intr_number ));
	sc->sc_subdev = config_found_sm(self, &uca, ucomprint, ucomsubmatch);
}

int
umct_detach(struct device *self, int flags)
{
	struct umct_softc *sc = (struct umct_softc *)self;
	int rv = 0;

	DPRINTF(("umct_detach: sc=%p flags=%d\n", sc, flags));

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

void
umct_set_line_state(struct umct_softc *sc)
{
	usb_device_request_t req;
	uByte ls;

	ls = (sc->sc_dtr ? MCR_DTR : 0) |
	     (sc->sc_rts ? MCR_RTS : 0);

	DPRINTF(("umct_set_line_state: DTR=%d,RTS=%d,ls=%02x\n",
			sc->sc_dtr, sc->sc_rts, ls));

	req.bmRequestType = UMCT_SET_REQUEST;
	req.bRequest = REQ_SET_MCR;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_iface_number);
	USETW(req.wLength, LENGTH_SET_MCR);

	(void)usbd_do_request(sc->sc_udev, &req, &ls);
}

void
umct_set(void *addr, int portno, int reg, int onoff)
{
	struct umct_softc *sc = addr;

	switch (reg) {
	case UCOM_SET_DTR:
		umct_dtr(sc, onoff);
		break;
	case UCOM_SET_RTS:
		umct_rts(sc, onoff);
		break;
	case UCOM_SET_BREAK:
		umct_break(sc, onoff);
		break;
	default:
		break;
	}
}

void
umct_dtr(struct umct_softc *sc, int onoff)
{

	DPRINTF(("umct_dtr: onoff=%d\n", onoff));

	if (sc->sc_dtr == onoff)
		return;
	sc->sc_dtr = onoff;

	umct_set_line_state(sc);
}

void
umct_rts(struct umct_softc *sc, int onoff)
{
	DPRINTF(("umct_rts: onoff=%d\n", onoff));

	if (sc->sc_rts == onoff)
		return;
	sc->sc_rts = onoff;

	umct_set_line_state(sc);
}

void
umct_break(struct umct_softc *sc, int onoff)
{
	DPRINTF(("umct_break: onoff=%d\n", onoff));

	umct_set_lcr(sc, onoff ? sc->last_lcr | LCR_SET_BREAK :
		     sc->last_lcr);
}

void
umct_set_lcr(struct umct_softc *sc, u_int data)
{
	usb_device_request_t req;
	uByte adata;

	adata = data;
	req.bmRequestType = UMCT_SET_REQUEST;
	req.bRequest = REQ_SET_LCR;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_iface_number);
	USETW(req.wLength, LENGTH_SET_LCR);

	/* XXX should check */
	(void)usbd_do_request(sc->sc_udev, &req, &adata);
}

void
umct_set_baudrate(struct umct_softc *sc, u_int rate, int cts)
{
        usb_device_request_t req;
	uDWord arate;
	u_int val;

	if (sc->sc_product == USB_PRODUCT_MCT_SITECOM_USB232 ||
	    sc->sc_product == USB_PRODUCT_BELKIN_F5U109) {
		switch (rate) {
		case    300: val = 0x01; break;
		case    600: val = 0x02; break;
		case   1200: val = 0x03; break;
		case   2400: val = 0x04; break;
		case   4800: val = 0x06; break;
		case   9600: val = 0x08; break;
		case  19200: val = 0x09; break;
		case  38400: val = 0x0a; break;
		case  57600: val = 0x0b; break;
		case 115200: val = 0x0c; break;
		default:     val = -1; break;
		}
	} else {
		val = UMCT_BAUD_RATE(rate);
	}
	USETDW(arate, val);

        req.bmRequestType = UMCT_SET_REQUEST;
        req.bRequest = REQ_SET_BAUD_RATE;
        USETW(req.wValue, 0);
        USETW(req.wIndex, sc->sc_iface_number);
        USETW(req.wLength, LENGTH_BAUD_RATE);

	/* XXX should check */
        (void)usbd_do_request(sc->sc_udev, &req, arate);

	/* unknown request, required after setting baud rate */
	USETDW(arate, 0);
	req.bmRequestType = UMCT_SET_REQUEST;
	req.bRequest = REQ_UNKNOWN1;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_iface_number);
	USETW(req.wLength, LENGTH_UNKNOWN1);
	(void)usbd_do_request(sc->sc_udev, &req, arate);

	/* update CTS, also required after setting baud rate */
	USETDW(arate, cts);
	req.bmRequestType = UMCT_SET_REQUEST;
	req.bRequest = REQ_SET_CTS;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_iface_number);
	USETW(req.wLength, LENGTH_SET_CTS);
	(void)usbd_do_request(sc->sc_udev, &req, arate);
}

void
umct_init(struct umct_softc *sc)
{
	umct_set_baudrate(sc, 9600, 0);
	umct_set_lcr(sc, LCR_DATA_BITS_8 | LCR_PARITY_NONE | LCR_STOP_BITS_1);
}

int
umct_param(void *addr, int portno, struct termios *t)
{
	struct umct_softc *sc = addr;
	u_int data = 0;

	DPRINTF(("umct_param: sc=%p\n", sc));

	DPRINTF(("umct_param: BAUDRATE=%d\n", t->c_ospeed));

	if (ISSET(t->c_cflag, CSTOPB))
		data |= LCR_STOP_BITS_2;
	else
		data |= LCR_STOP_BITS_1;
	if (ISSET(t->c_cflag, PARENB)) {
		if (ISSET(t->c_cflag, PARODD))
			data |= LCR_PARITY_ODD;
		else
			data |= LCR_PARITY_EVEN;
	} else
		data |= LCR_PARITY_NONE;
	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
		data |= LCR_DATA_BITS_5;
		break;
	case CS6:
		data |= LCR_DATA_BITS_6;
		break;
	case CS7:
		data |= LCR_DATA_BITS_7;
		break;
	case CS8:
		data |= LCR_DATA_BITS_8;
		break;
	}

	umct_set_baudrate(sc, t->c_ospeed, ISSET(t->c_cflag, CRTSCTS));

	sc->last_lcr = data;
	umct_set_lcr(sc, data);

	return (0);
}

int
umct_open(void *addr, int portno)
{
	struct umct_softc *sc = addr;
	int err, lcr_data;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	DPRINTF(("umct_open: sc=%p\n", sc));

	/* initialize LCR */
        lcr_data = LCR_DATA_BITS_8 | LCR_PARITY_NONE |
	    LCR_STOP_BITS_1;
        umct_set_lcr(sc, lcr_data);

	if (sc->sc_intr_number != -1 && sc->sc_intr_pipe == NULL) {
		sc->sc_status = 0; /* clear status bit */
		sc->sc_intr_buf = malloc(sc->sc_isize, M_USBDEV, M_WAITOK);
		err = usbd_open_pipe_intr(sc->sc_iface, sc->sc_intr_number,
			USBD_SHORT_XFER_OK, &sc->sc_intr_pipe, sc,
			sc->sc_intr_buf, sc->sc_isize,
			umct_intr, USBD_DEFAULT_INTERVAL);
		if (err) {
			DPRINTF(("%s: cannot open interrupt pipe (addr %d)\n",
				sc->sc_dev.dv_xname, sc->sc_intr_number));
					return (EIO);
		}
	}

	return (0);
}

void
umct_close(void *addr, int portno)
{
	struct umct_softc *sc = addr;
	int err;

	if (usbd_is_dying(sc->sc_udev))
		return;

	DPRINTF(("umct_close: close\n"));

	if (sc->sc_intr_pipe != NULL) {
		err = usbd_close_pipe(sc->sc_intr_pipe);
		if (err)
			printf("%s: close interrupt pipe failed: %s\n",
				sc->sc_dev.dv_xname, usbd_errstr(err));
		free(sc->sc_intr_buf, M_USBDEV, sc->sc_isize);
		sc->sc_intr_pipe = NULL;
	}
}

void
umct_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct umct_softc *sc = priv;
	u_char *buf = sc->sc_intr_buf;
	u_char mstatus;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		DPRINTF(("%s: abnormal status: %s\n", sc->sc_dev.dv_xname,
			usbd_errstr(status)));
		usbd_clear_endpoint_stall_async(sc->sc_intr_pipe);
		return;
	}

	DPRINTF(("%s: umct status = MSR:%02x, LSR:%02x\n",
		 sc->sc_dev.dv_xname, buf[0],buf[1]));

	sc->sc_lsr = sc->sc_msr = 0;
	mstatus = buf[0];
	if (ISSET(mstatus, MSR_DSR))
		sc->sc_msr |= UMSR_DSR;
	if (ISSET(mstatus, MSR_DCD))
		sc->sc_msr |= UMSR_DCD;
	ucom_status_change((struct ucom_softc *)sc->sc_subdev);
}

void
umct_get_status(void *addr, int portno, u_char *lsr, u_char *msr)
{
	struct umct_softc *sc = addr;

	DPRINTF(("umct_get_status:\n"));

	if (lsr != NULL)
		*lsr = sc->sc_lsr;
	if (msr != NULL)
		*msr = sc->sc_msr;
}
