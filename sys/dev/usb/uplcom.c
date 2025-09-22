/*	$OpenBSD: uplcom.c,v 1.81 2024/05/23 03:21:09 jsg Exp $	*/
/*	$NetBSD: uplcom.c,v 1.29 2002/09/23 05:51:23 simonb Exp $	*/
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
 * Simple datasheet
 * http://www.prolific.com.tw/PDF/PL-2303%20Market%20Spec.pdf
 * http://www.hitachi-hitec.com/jyouhou/prolific/2303.pdf
 * 	(english)
 *
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

#ifdef UPLCOM_DEBUG
#define DPRINTFN(n, x)  do { if (uplcomdebug > (n)) printf x; } while (0)
int	uplcomdebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x) DPRINTFN(0, x)

#define	UPLCOM_IFACE_INDEX	0
#define	UPLCOM_SECOND_IFACE_INDEX	1

#define	UPLCOM_SET_REQUEST	0x01
#define	UPLCOM_HXN_SET_REQUEST	0x80
#define	UPLCOM_HXN_SET_CRTSCTS_REG 0x0A
#define	UPLCOM_SET_CRTSCTS	0x41
#define	UPLCOM_HX_SET_CRTSCTS	0x61
#define	UPLCOM_HXN_SET_CRTSCTS	0xFA
#define	UPLCOM_HX_STATUS_REG	0x8080
#define RSAQ_STATUS_CTS		0x80
#define RSAQ_STATUS_DSR		0x02
#define RSAQ_STATUS_DCD		0x01

#define UPLCOM_TYPE_01		0
#define UPLCOM_TYPE_HX		1
#define UPLCOM_TYPE_HXN		2

struct	uplcom_softc {
	struct device		 sc_dev;	/* base device */
	struct usbd_device	*sc_udev;	/* USB device */
	struct usbd_interface	*sc_iface;	/* interface */
	int			 sc_iface_number;	/* interface number */

	struct usbd_interface	*sc_intr_iface;	/* interrupt interface */
	int			 sc_intr_number;	/* interrupt number */
	struct usbd_pipe	*sc_intr_pipe;	/* interrupt pipe */
	u_char			*sc_intr_buf;	/* interrupt buffer */
	int			 sc_isize;

	struct usb_cdc_line_state sc_line_state;/* current line state */
	int			 sc_dtr;	/* current DTR state */
	int			 sc_rts;	/* current RTS state */

	struct device		*sc_subdev;	/* ucom device */

	u_char			 sc_lsr;	/* Local status register */
	u_char			 sc_msr;	/* uplcom status register */
	int			 sc_type;	/* variant */
};

/*
 * These are the maximum number of bytes transferred per frame.
 * The output buffer size cannot be increased due to the size encoding.
 */
#define UPLCOMIBUFSIZE 256
#define UPLCOMOBUFSIZE 256

usbd_status uplcom_reset(struct uplcom_softc *);
usbd_status uplcom_set_line_coding(struct uplcom_softc *sc,
    struct usb_cdc_line_state *state);
usbd_status uplcom_set_crtscts(struct uplcom_softc *);
void uplcom_intr(struct usbd_xfer *, void *, usbd_status);

void uplcom_set(void *, int, int, int);
void uplcom_dtr(struct uplcom_softc *, int);
void uplcom_rts(struct uplcom_softc *, int);
void uplcom_break(struct uplcom_softc *, int);
void uplcom_set_line_state(struct uplcom_softc *);
void uplcom_get_status(void *, int portno, u_char *lsr, u_char *msr);
int  uplcom_param(void *, int, struct termios *);
int  uplcom_open(void *, int);
void uplcom_close(void *, int);

const struct ucom_methods uplcom_methods = {
	uplcom_get_status,
	uplcom_set,
	uplcom_param,
	NULL,
	uplcom_open,
	uplcom_close,
	NULL,
	NULL,
};

static const struct usb_devno uplcom_devs[] = {
	{ USB_VENDOR_ALCATEL, USB_PRODUCT_ALCATEL_OT535 },
	{ USB_VENDOR_ANCHOR, USB_PRODUCT_ANCHOR_SERIAL },
	{ USB_VENDOR_ATEN, USB_PRODUCT_ATEN_UC232A },
	{ USB_VENDOR_BELKIN, USB_PRODUCT_BELKIN_F5U257 },
	{ USB_VENDOR_ELECOM, USB_PRODUCT_ELECOM_UCSGT },
	{ USB_VENDOR_ELECOM, USB_PRODUCT_ELECOM_UCSGT0 },
	{ USB_VENDOR_HAL, USB_PRODUCT_HAL_IMR001 },
	{ USB_VENDOR_HP, USB_PRODUCT_HP_LD220 },
	{ USB_VENDOR_IODATA, USB_PRODUCT_IODATA_USBRSAQ },
	{ USB_VENDOR_IODATA, USB_PRODUCT_IODATA_USBRSAQ5 },
	{ USB_VENDOR_LEADTEK, USB_PRODUCT_LEADTEK_9531 },
	{ USB_VENDOR_MICROSOFT, USB_PRODUCT_MICROSOFT_700WX },
	{ USB_VENDOR_MOBILEACTION, USB_PRODUCT_MOBILEACTION_MA620 },
	{ USB_VENDOR_NOKIA, USB_PRODUCT_NOKIA_CA42 },
	{ USB_VENDOR_OTI, USB_PRODUCT_OTI_DKU5 },
	{ USB_VENDOR_PLX, USB_PRODUCT_PLX_CA42 },
	{ USB_VENDOR_PANASONIC, USB_PRODUCT_PANASONIC_TYTP50P6S },
	{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2303 },
	{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2303GB },
	{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2303GC },
	{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2303GE },
	{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2303GL },
	{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2303GS },
	{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2303GT },
	{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2303X },
	{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2303X2 },
	{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_RSAQ2 },
	{ USB_VENDOR_PROLIFIC, USB_PRODUCT_PROLIFIC_PL2303BENQ },
	{ USB_VENDOR_PROLIFIC2, USB_PRODUCT_PROLIFIC2_PL2303 },
	{ USB_VENDOR_RADIOSHACK, USB_PRODUCT_RADIOSHACK_PL2303 },
	{ USB_VENDOR_RATOC, USB_PRODUCT_RATOC_REXUSB60 },
	{ USB_VENDOR_SAGEM, USB_PRODUCT_SAGEM_SERIAL },
	{ USB_VENDOR_SIEMENS3, USB_PRODUCT_SIEMENS3_SX1 },
	{ USB_VENDOR_SIEMENS3, USB_PRODUCT_SIEMENS3_X65 },
	{ USB_VENDOR_SIEMENS3, USB_PRODUCT_SIEMENS3_X75 },
	{ USB_VENDOR_SITECOM, USB_PRODUCT_SITECOM_CN104 },
	{ USB_VENDOR_SOURCENEXT, USB_PRODUCT_SOURCENEXT_KEIKAI8 },
	{ USB_VENDOR_SOURCENEXT, USB_PRODUCT_SOURCENEXT_KEIKAI8_CHG },
	{ USB_VENDOR_SPEEDDRAGON, USB_PRODUCT_SPEEDDRAGON_MS3303H },
	{ USB_VENDOR_SUSTEEN, USB_PRODUCT_SUSTEEN_DCU11 },
	{ USB_VENDOR_SYNTECH, USB_PRODUCT_SYNTECH_SERIAL },
	{ USB_VENDOR_TDK, USB_PRODUCT_TDK_UHA6400 },
	{ USB_VENDOR_TDK, USB_PRODUCT_TDK_UPA9664 },
	{ USB_VENDOR_TRIPPLITE, USB_PRODUCT_TRIPPLITE_U209 },
	{ USB_VENDOR_SMART, USB_PRODUCT_SMART_PL2303 },
	{ USB_VENDOR_YCCABLE, USB_PRODUCT_YCCABLE_PL2303 }
};
#define uplcom_lookup(v, p) usb_lookup(uplcom_devs, v, p)

int uplcom_match(struct device *, void *, void *);
void uplcom_attach(struct device *, struct device *, void *);
int uplcom_detach(struct device *, int);

struct cfdriver uplcom_cd = {
	NULL, "uplcom", DV_DULL
};

const struct cfattach uplcom_ca = {
	sizeof(struct uplcom_softc), uplcom_match, uplcom_attach, uplcom_detach
};

int
uplcom_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	return (uplcom_lookup(uaa->vendor, uaa->product) != NULL ?
		UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
uplcom_attach(struct device *parent, struct device *self, void *aux)
{
	struct uplcom_softc *sc = (struct uplcom_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_device *dev = uaa->device;
	usb_config_descriptor_t *cdesc;
	usb_device_descriptor_t *ddesc;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	char *devname = sc->sc_dev.dv_xname;
	usb_device_request_t req;
	usbd_status err;
	uByte val;
	int i;
	struct ucom_attach_args uca;

	sc->sc_udev = dev;

	DPRINTF(("\n\nuplcom attach: sc=%p\n", sc));

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

	/* get the device descriptor */
	ddesc = usbd_get_device_descriptor(sc->sc_udev);
	if (ddesc == NULL) {
		printf("%s: failed to get device descriptor\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	/*
	 * The Linux driver suggest this will only be true for the HX
	 * variants. The datasheets disagree.
	 */
	if (ddesc->bDeviceClass == 0x02)
		sc->sc_type = UPLCOM_TYPE_01;
	else if (ddesc->bMaxPacketSize == 0x40)
		sc->sc_type = UPLCOM_TYPE_HX;
	else
		sc->sc_type = UPLCOM_TYPE_01;

	if (sc->sc_type == UPLCOM_TYPE_HX) {
		req.bmRequestType = UT_READ_VENDOR_DEVICE;
		req.bRequest = UPLCOM_SET_REQUEST;
		USETW(req.wValue, UPLCOM_HX_STATUS_REG);
		USETW(req.wIndex, sc->sc_iface_number);
		USETW(req.wLength, 1);
		err = usbd_do_request(sc->sc_udev, &req, &val);
		if (err)
			sc->sc_type = UPLCOM_TYPE_HXN;
	}

#ifdef UPLCOM_DEBUG
	/* print the chip type */
	if (sc->sc_type == UPLCOM_TYPE_HXN) {
		DPRINTF(("uplcom_attach: chiptype 2303XN\n"));
	} else if (sc->sc_type == UPLCOM_TYPE_HX) {
		DPRINTF(("uplcom_attach: chiptype 2303X\n"));
	} else {
		DPRINTF(("uplcom_attach: chiptype 2303\n"));
	}
#endif
	/* get the (first/common) interface */
	err = usbd_device2interface_handle(dev, UPLCOM_IFACE_INDEX,
							&sc->sc_iface);
	if (err) {
		printf("\n%s: failed to get interface, err=%s\n",
			devname, usbd_errstr(err));
		usbd_deactivate(sc->sc_udev);
		return;
	}

	/* Find the interrupt endpoints */

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

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->sc_intr_number = ed->bEndpointAddress;
			sc->sc_isize = UGETW(ed->wMaxPacketSize);
		}
	}

	if (sc->sc_intr_number== -1) {
		printf("%s: Could not find interrupt in\n",
			sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	/* keep interface for interrupt */
	sc->sc_intr_iface = sc->sc_iface;

	/*
	 * USB-RSAQ1 has two interface
	 *
	 *  USB-RSAQ1       | USB-RSAQ2
	 * -----------------+-----------------
	 * Interface 0      |Interface 0
	 *  Interrupt(0x81) | Interrupt(0x81)
	 * -----------------+ BulkIN(0x02)
	 * Interface 1	    | BulkOUT(0x83)
	 *   BulkIN(0x02)   |
	 *   BulkOUT(0x83)  |
	 */
	if (cdesc->bNumInterfaces == 2) {
		err = usbd_device2interface_handle(dev,
				UPLCOM_SECOND_IFACE_INDEX, &sc->sc_iface);
		if (err) {
			printf("\n%s: failed to get second interface, err=%s\n",
			    devname, usbd_errstr(err));
			usbd_deactivate(sc->sc_udev);
			return;
		}
	}

	/* Find the bulk{in,out} endpoints */

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

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			uca.bulkin = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			uca.bulkout = ed->bEndpointAddress;
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

	sc->sc_dtr = sc->sc_rts = -1;
	uca.portno = UCOM_UNK_PORTNO;
	/* bulkin, bulkout set above */
	uca.ibufsize = UPLCOMIBUFSIZE;
	uca.obufsize = UPLCOMOBUFSIZE;
	uca.ibufsizepad = UPLCOMIBUFSIZE;
	uca.opkthdrlen = 0;
	uca.device = dev;
	uca.iface = sc->sc_iface;
	uca.methods = &uplcom_methods;
	uca.arg = sc;
	uca.info = NULL;

	if (sc->sc_type != UPLCOM_TYPE_HXN) {
		err = uplcom_reset(sc);
		if (err) {
			printf("%s: reset failed, %s\n", sc->sc_dev.dv_xname,
				usbd_errstr(err));
			usbd_deactivate(sc->sc_udev);
			return;
		}
	}

	DPRINTF(("uplcom: in=0x%x out=0x%x intr=0x%x\n",
			uca.bulkin, uca.bulkout, sc->sc_intr_number ));
	sc->sc_subdev = config_found_sm(self, &uca, ucomprint, ucomsubmatch);
}

int
uplcom_detach(struct device *self, int flags)
{
	struct uplcom_softc *sc = (struct uplcom_softc *)self;
	int rv = 0;

	DPRINTF(("uplcom_detach: sc=%p flags=%d\n", sc, flags));

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
uplcom_reset(struct uplcom_softc *sc)
{
	usb_device_request_t req;
	usbd_status err;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UPLCOM_SET_REQUEST;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_iface_number);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_udev, &req, 0);
	if (err)
		return (EIO);

	return (0);
}

void
uplcom_set_line_state(struct uplcom_softc *sc)
{
	usb_device_request_t req;
	int ls;

	/* Make sure we have initialized state for sc_dtr and sc_rts */
	if (sc->sc_dtr == -1)
		sc->sc_dtr = 0;
	if (sc->sc_rts == -1)
		sc->sc_rts = 0;

	ls = (sc->sc_dtr ? UCDC_LINE_DTR : 0) |
	    (sc->sc_rts ? UCDC_LINE_RTS : 0);

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_CONTROL_LINE_STATE;
	USETW(req.wValue, ls);
	USETW(req.wIndex, sc->sc_iface_number);
	USETW(req.wLength, 0);

	(void)usbd_do_request(sc->sc_udev, &req, 0);

}

void
uplcom_set(void *addr, int portno, int reg, int onoff)
{
	struct uplcom_softc *sc = addr;

	switch (reg) {
	case UCOM_SET_DTR:
		uplcom_dtr(sc, onoff);
		break;
	case UCOM_SET_RTS:
		uplcom_rts(sc, onoff);
		break;
	case UCOM_SET_BREAK:
		uplcom_break(sc, onoff);
		break;
	default:
		break;
	}
}

void
uplcom_dtr(struct uplcom_softc *sc, int onoff)
{

	DPRINTF(("uplcom_dtr: onoff=%d\n", onoff));

	if (sc->sc_dtr != -1 && !sc->sc_dtr == !onoff)
		return;

	sc->sc_dtr = !!onoff;

	uplcom_set_line_state(sc);
}

void
uplcom_rts(struct uplcom_softc *sc, int onoff)
{
	DPRINTF(("uplcom_rts: onoff=%d\n", onoff));

	if (sc->sc_rts == -1 && !sc->sc_rts == !onoff)
		return;

	sc->sc_rts = !!onoff;

	uplcom_set_line_state(sc);
}

void
uplcom_break(struct uplcom_softc *sc, int onoff)
{
	usb_device_request_t req;

	DPRINTF(("uplcom_break: onoff=%d\n", onoff));

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SEND_BREAK;
	USETW(req.wValue, onoff ? UCDC_BREAK_ON : UCDC_BREAK_OFF);
	USETW(req.wIndex, sc->sc_iface_number);
	USETW(req.wLength, 0);

	(void)usbd_do_request(sc->sc_udev, &req, 0);
}

usbd_status
uplcom_set_crtscts(struct uplcom_softc *sc)
{
	usb_device_request_t req;
	usbd_status err;

	DPRINTF(("uplcom_set_crtscts: on\n"));

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	if (sc->sc_type == UPLCOM_TYPE_HXN) {
		req.bRequest = UPLCOM_HXN_SET_REQUEST;
		USETW(req.wValue, UPLCOM_HXN_SET_CRTSCTS_REG);
		USETW(req.wIndex, UPLCOM_HXN_SET_CRTSCTS);
	} else {
		req.bRequest = UPLCOM_SET_REQUEST;
		USETW(req.wValue, 0);
		USETW(req.wIndex, (sc->sc_type == UPLCOM_TYPE_HX ?
		    UPLCOM_HX_SET_CRTSCTS : UPLCOM_SET_CRTSCTS));
	}
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_udev, &req, 0);
	if (err) {
		DPRINTF(("uplcom_set_crtscts: failed, err=%s\n",
			usbd_errstr(err)));
		return (err);
	}

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uplcom_set_line_coding(struct uplcom_softc *sc,
    struct usb_cdc_line_state *state)
{
	usb_device_request_t req;
	usbd_status err;

	DPRINTF(("uplcom_set_line_coding: rate=%d fmt=%d parity=%d bits=%d\n",
		UGETDW(state->dwDTERate), state->bCharFormat,
		state->bParityType, state->bDataBits));

	if (memcmp(state, &sc->sc_line_state, UCDC_LINE_STATE_LENGTH) == 0) {
		DPRINTF(("uplcom_set_line_coding: already set\n"));
		return (USBD_NORMAL_COMPLETION);
	}

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_LINE_CODING;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_iface_number);
	USETW(req.wLength, UCDC_LINE_STATE_LENGTH);

	err = usbd_do_request(sc->sc_udev, &req, state);
	if (err) {
		DPRINTF(("uplcom_set_line_coding: failed, err=%s\n",
			usbd_errstr(err)));
		return (err);
	}

	sc->sc_line_state = *state;

	return (USBD_NORMAL_COMPLETION);
}

int
uplcom_param(void *addr, int portno, struct termios *t)
{
	struct uplcom_softc *sc = addr;
	usbd_status err;
	struct usb_cdc_line_state ls;

	DPRINTF(("uplcom_param: sc=%p\n", sc));

	USETDW(ls.dwDTERate, t->c_ospeed);
	if (ISSET(t->c_cflag, CSTOPB))
		ls.bCharFormat = UCDC_STOP_BIT_2;
	else
		ls.bCharFormat = UCDC_STOP_BIT_1;
	if (ISSET(t->c_cflag, PARENB)) {
		if (ISSET(t->c_cflag, PARODD))
			ls.bParityType = UCDC_PARITY_ODD;
		else
			ls.bParityType = UCDC_PARITY_EVEN;
	} else
		ls.bParityType = UCDC_PARITY_NONE;
	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
		ls.bDataBits = 5;
		break;
	case CS6:
		ls.bDataBits = 6;
		break;
	case CS7:
		ls.bDataBits = 7;
		break;
	case CS8:
		ls.bDataBits = 8;
		break;
	}

	err = uplcom_set_line_coding(sc, &ls);
	if (err) {
		DPRINTF(("uplcom_param: err=%s\n", usbd_errstr(err)));
		return (EIO);
	}

	if (ISSET(t->c_cflag, CRTSCTS))
		uplcom_set_crtscts(sc);

	if (sc->sc_rts == -1 || sc->sc_dtr == -1)
		uplcom_set_line_state(sc);

	return (0);
}

int
uplcom_open(void *addr, int portno)
{
	struct uplcom_softc *sc = addr;
	usb_device_request_t req;
	usbd_status uerr;
	int err;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	DPRINTF(("uplcom_open: sc=%p\n", sc));

	if (sc->sc_intr_number != -1 && sc->sc_intr_pipe == NULL) {
		sc->sc_intr_buf = malloc(sc->sc_isize, M_USBDEV, M_WAITOK);
		err = usbd_open_pipe_intr(sc->sc_intr_iface, sc->sc_intr_number,
			USBD_SHORT_XFER_OK, &sc->sc_intr_pipe, sc,
			sc->sc_intr_buf, sc->sc_isize,
			uplcom_intr, USBD_DEFAULT_INTERVAL);
		if (err) {
			DPRINTF(("%s: cannot open interrupt pipe (addr %d)\n",
				sc->sc_dev.dv_xname, sc->sc_intr_number));
					return (EIO);
		}
	}

	if (sc->sc_type == UPLCOM_TYPE_HX) {
		/*
		 * Undocumented (vendor unresponsive) - possibly changes
		 * flow control semantics. It is needed for HX variant devices.
		 */
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
		req.bRequest = UPLCOM_SET_REQUEST;
		USETW(req.wValue, 2);
		USETW(req.wIndex, 0x44);
		USETW(req.wLength, 0);

		uerr = usbd_do_request(sc->sc_udev, &req, 0);
		if (uerr)
			return (EIO);

		/* Reset upstream data pipes */
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
		req.bRequest = UPLCOM_SET_REQUEST;
		USETW(req.wValue, 8);
		USETW(req.wIndex, 0);
		USETW(req.wLength, 0);

		uerr = usbd_do_request(sc->sc_udev, &req, 0);
		if (uerr)
			return (EIO);

		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
		req.bRequest = UPLCOM_SET_REQUEST;
		USETW(req.wValue, 9);
		USETW(req.wIndex, 0);
		USETW(req.wLength, 0);

		uerr = usbd_do_request(sc->sc_udev, &req, 0);
		if (uerr)
			return (EIO);
	}

	if (sc->sc_type == UPLCOM_TYPE_HXN) {
		/* Reset upstream data pipes */
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
		req.bRequest = UPLCOM_HXN_SET_REQUEST;
		USETW(req.wValue, 0x07);
		USETW(req.wIndex, 0x03);
		USETW(req.wLength, 0);

		uerr = usbd_do_request(sc->sc_udev, &req, 0);
		if (uerr)
			return (EIO);
	}

	return (0);
}

void
uplcom_close(void *addr, int portno)
{
	struct uplcom_softc *sc = addr;
	int err;

	if (usbd_is_dying(sc->sc_udev))
		return;

	DPRINTF(("uplcom_close: close\n"));

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
uplcom_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct uplcom_softc *sc = priv;
	u_char *buf = sc->sc_intr_buf;
	u_char pstatus;

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

	DPRINTF(("%s: uplcom status = %02x\n", sc->sc_dev.dv_xname, buf[8]));

	sc->sc_lsr = sc->sc_msr = 0;
	pstatus = buf[8];
	if (ISSET(pstatus, RSAQ_STATUS_CTS))
		sc->sc_msr |= UMSR_CTS;
	else
		sc->sc_msr &= ~UMSR_CTS;
	if (ISSET(pstatus, RSAQ_STATUS_DSR))
		sc->sc_msr |= UMSR_DSR;
	else
		sc->sc_msr &= ~UMSR_DSR;
	if (ISSET(pstatus, RSAQ_STATUS_DCD))
		sc->sc_msr |= UMSR_DCD;
	else
		sc->sc_msr &= ~UMSR_DCD;
	ucom_status_change((struct ucom_softc *) sc->sc_subdev);
}

void
uplcom_get_status(void *addr, int portno, u_char *lsr, u_char *msr)
{
	struct uplcom_softc *sc = addr;

	DPRINTF(("uplcom_get_status:\n"));

	if (lsr != NULL)
		*lsr = sc->sc_lsr;
	if (msr != NULL)
		*msr = sc->sc_msr;
}
