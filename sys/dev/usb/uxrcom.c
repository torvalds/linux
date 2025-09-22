/*	$OpenBSD: uxrcom.c,v 1.4 2024/05/23 03:21:09 jsg Exp $	*/

/*
 * Copyright (c) 2006 Jonathan Gray <jsg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/device.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbcdc.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/ucomvar.h>

#ifdef UXRCOM_DEBUG
#define DPRINTFN(n, x)  do { if (uxrcomdebug > (n)) printf x; } while (0)
int	uxrcomdebug = 0;
#else
#define DPRINTFN(n, x)
#endif
#define DPRINTF(x) DPRINTFN(0, x)

#define UXRCOMBUFSZ		64
#define UXRCOM_INTR_IFACE_NO	0
#define UXRCOM_DATA_IFACE_NO	1

#define XR_SET_REG		0
#define XR_GET_REGN		1

#define XR_FLOW_CONTROL		0x000c
#define  XR_FLOW_CONTROL_ON	1
#define  XR_FLOW_CONTROL_OFF	0
#define XR_TX_BREAK		0x0014
#define  XR_TX_BREAK_ON		1
#define  XR_TX_BREAK_OFF	0
#define XR_GPIO_SET		0x001d
#define XR_GPIO_CLEAR		0x001e
#define  XR_GPIO3		(1 << 3)
#define  XR_GPIO5		(1 << 5)

struct uxrcom_softc {
	struct device		sc_dev;
	struct usbd_device	*sc_udev;
	struct usbd_interface	*sc_intr_iface;
	struct usbd_interface	*sc_data_iface;
	struct device		*sc_subdev;

	struct usb_cdc_line_state sc_line_state;

	int			sc_intr_number;
	struct usbd_pipe	*sc_intr_pipe;
	struct usb_cdc_notification sc_intr_buf;
	u_char			sc_msr;
	u_char			sc_lsr;
};

void	uxrcom_get_status(void *, int, u_char *, u_char *);
void	uxrcom_set(void *, int, int, int);
int	uxrcom_param(void *, int, struct termios *);
int	uxrcom_open(void *, int);
void	uxrcom_close(void *, int);
void	uxrcom_break(void *, int, int);
void	uxrcom_intr(struct usbd_xfer *, void *, usbd_status);

const struct ucom_methods uxrcom_methods = {
	uxrcom_get_status,
	uxrcom_set,
	uxrcom_param,
	NULL,
	uxrcom_open,
	uxrcom_close,
	NULL,
	NULL,
};

static const struct usb_devno uxrcom_devs[] = {
	{ USB_VENDOR_EXAR,		USB_PRODUCT_EXAR_XR21V1410 },
};

int uxrcom_match(struct device *, void *, void *);
void uxrcom_attach(struct device *, struct device *, void *);
int uxrcom_detach(struct device *, int);

struct cfdriver uxrcom_cd = {
	NULL, "uxrcom", DV_DULL
};

const struct cfattach uxrcom_ca = {
	sizeof(struct uxrcom_softc), uxrcom_match, uxrcom_attach, uxrcom_detach
};

int
uxrcom_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL)
		return UMATCH_NONE;
	if (uaa->ifaceno != UXRCOM_INTR_IFACE_NO)
		return UMATCH_NONE;

	return (usb_lookup(uxrcom_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

void
uxrcom_attach(struct device *parent, struct device *self, void *aux)
{
	struct uxrcom_softc *sc = (struct uxrcom_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct ucom_attach_args uca;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	usbd_status error;
	int i;

	memset(&uca, 0, sizeof(uca));
	sc->sc_udev = uaa->device;
	sc->sc_intr_iface = uaa->iface;
	sc->sc_intr_number = -1;

	id = usbd_get_interface_descriptor(sc->sc_intr_iface);
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_intr_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor found for %d\n",
			    sc->sc_dev.dv_xname, i);
			usbd_deactivate(sc->sc_udev);
			return;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT)
			sc->sc_intr_number = ed->bEndpointAddress;
	}

	error = usbd_device2interface_handle(sc->sc_udev, UXRCOM_DATA_IFACE_NO,
	    &sc->sc_data_iface);
	if (error != 0) {
		printf("%s: could not get data interface handle\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	id = usbd_get_interface_descriptor(sc->sc_data_iface);
	uca.bulkin = uca.bulkout = -1;
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_data_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor found for %d\n",
			    sc->sc_dev.dv_xname, i);
			usbd_deactivate(sc->sc_udev);
			return;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			uca.bulkin = ed->bEndpointAddress;
		else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK)
			uca.bulkout = ed->bEndpointAddress;
	}

	if (uca.bulkin == -1 || uca.bulkout == -1) {
		printf("%s: missing endpoint\n", sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	usbd_claim_iface(sc->sc_udev, UXRCOM_DATA_IFACE_NO);

	uca.ibufsize = UXRCOMBUFSZ;
	uca.obufsize = UXRCOMBUFSZ;
	uca.ibufsizepad = UXRCOMBUFSZ;
	uca.opkthdrlen = 0;
	uca.device = sc->sc_udev;
	uca.iface = sc->sc_data_iface;
	uca.methods = &uxrcom_methods;
	uca.arg = sc;
	uca.info = NULL;

	sc->sc_subdev = config_found_sm(self, &uca, ucomprint, ucomsubmatch);
}

int
uxrcom_detach(struct device *self, int flags)
{
	struct uxrcom_softc *sc = (struct uxrcom_softc *)self;
	int rv = 0;

	if (sc->sc_intr_pipe != NULL) {
		usbd_close_pipe(sc->sc_intr_pipe);
		sc->sc_intr_pipe = NULL;
	}

	if (sc->sc_subdev != NULL)
		rv = config_detach(sc->sc_subdev, flags);

	return rv;
}

int
uxrcom_open(void *vsc, int portno)
{
	struct uxrcom_softc *sc = vsc;
	int err;

	if (sc->sc_intr_number != -1 && sc->sc_intr_pipe == NULL) {
		err = usbd_open_pipe_intr(sc->sc_intr_iface,
		    sc->sc_intr_number, USBD_SHORT_XFER_OK, &sc->sc_intr_pipe,
		    sc, &sc->sc_intr_buf, sizeof(sc->sc_intr_buf),
		    uxrcom_intr, USBD_DEFAULT_INTERVAL);
		if (err)
			return EIO;
	}

	return 0;
}

void
uxrcom_close(void *vsc, int portno)
{
	struct uxrcom_softc *sc = vsc;
	int err;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (sc->sc_intr_pipe != NULL) {
		err = usbd_close_pipe(sc->sc_intr_pipe);
		if (err)
			printf("%s: close intr pipe failed: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(err));
		sc->sc_intr_pipe = NULL;
	}
}

void
uxrcom_set(void *vsc, int portno, int reg, int onoff)
{
	struct uxrcom_softc *sc = vsc;
	usb_device_request_t req;
	uint16_t index;
	uint8_t value;

	index = onoff ? XR_GPIO_SET : XR_GPIO_CLEAR;

	switch (reg) {
	case UCOM_SET_DTR:
		value = XR_GPIO3;
		break;
	case UCOM_SET_RTS:
		value = XR_GPIO5;
		break;
	case UCOM_SET_BREAK:
		uxrcom_break(sc, portno, onoff);
		return;
	default:
		return;
	}
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = XR_SET_REG;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, 0);
	usbd_do_request(sc->sc_udev, &req, NULL);
}

usbd_status
uxrcom_set_line_coding(struct uxrcom_softc *sc,
    struct usb_cdc_line_state *state)
{
	usb_device_request_t req;
	usbd_status err;

	if (memcmp(state, &sc->sc_line_state, UCDC_LINE_STATE_LENGTH) == 0)
		return USBD_NORMAL_COMPLETION;

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UCDC_SET_LINE_CODING;
	USETW(req.wValue, 0);
	USETW(req.wIndex, UXRCOM_INTR_IFACE_NO);
	USETW(req.wLength, UCDC_LINE_STATE_LENGTH);

	err = usbd_do_request(sc->sc_udev, &req, state);
	if (err)
		return err;

	sc->sc_line_state = *state;

	return USBD_NORMAL_COMPLETION;
}

int
uxrcom_param(void *vsc, int portno, struct termios *t)
{
	struct uxrcom_softc *sc = (struct uxrcom_softc *)vsc;
	usb_device_request_t req;
	usbd_status err;
	struct usb_cdc_line_state ls;
	uint8_t flowctrl;

	if (t->c_ospeed <= 0 || t->c_ospeed > 48000000)
		return (EINVAL);

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

	err = uxrcom_set_line_coding(sc, &ls);
	if (err)
		return (EIO);

	if (ISSET(t->c_cflag, CRTSCTS)) {
		/*  rts/cts flow ctl */
		flowctrl = XR_FLOW_CONTROL_ON;
	} else {
		/* disable flow ctl */
		flowctrl = XR_FLOW_CONTROL_OFF;
	}
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = XR_SET_REG;
	USETW(req.wValue, flowctrl);
	USETW(req.wIndex, XR_FLOW_CONTROL);
	USETW(req.wLength, 0);
	usbd_do_request(sc->sc_udev, &req, NULL);

	return (0);
}

void
uxrcom_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct uxrcom_softc *sc = priv;
	uint8_t mstatus;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;
		usbd_clear_endpoint_stall_async(sc->sc_intr_pipe);
		return;
	}

	if (sc->sc_intr_buf.bmRequestType != UCDC_NOTIFICATION)
		return;

	switch (sc->sc_intr_buf.bNotification) {
	case UCDC_N_SERIAL_STATE:
		sc->sc_lsr = sc->sc_msr = 0;
		mstatus = sc->sc_intr_buf.data[0];

		if (ISSET(mstatus, UCDC_N_SERIAL_RI))
			sc->sc_msr |= UMSR_RI;
		if (ISSET(mstatus, UCDC_N_SERIAL_DSR))
			sc->sc_msr |= UMSR_DSR;
		if (ISSET(mstatus, UCDC_N_SERIAL_DCD))
			sc->sc_msr |= UMSR_DCD;
		ucom_status_change((struct ucom_softc *)sc->sc_subdev);
		break;
	default:
		break;
	}
}

void
uxrcom_get_status(void *vsc, int portno, u_char *lsr, u_char *msr)
{
	struct uxrcom_softc *sc = vsc;
	
	if (msr != NULL)
		*msr = sc->sc_msr;
	if (lsr != NULL)
		*lsr = sc->sc_lsr;
}

void
uxrcom_break(void *vsc, int portno, int onoff)
{
	struct uxrcom_softc *sc = vsc;
	usb_device_request_t req;
	uint8_t brk = onoff ? UCDC_BREAK_ON : UCDC_BREAK_OFF;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = XR_SET_REG;
	USETW(req.wValue, brk);
	USETW(req.wIndex, XR_TX_BREAK);
	USETW(req.wLength, 0);
	usbd_do_request(sc->sc_udev, &req, NULL);
}
