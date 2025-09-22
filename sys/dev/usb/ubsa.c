/*	$OpenBSD: ubsa.c,v 1.71 2024/09/20 02:00:46 jsg Exp $ 	*/
/*	$NetBSD: ubsa.c,v 1.5 2002/11/25 00:51:33 fvdl Exp $	*/
/*-
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/tty.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbcdc.h>

#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/ucomvar.h>

#ifdef UBSA_DEBUG
int	ubsadebug = 0;

#define	DPRINTFN(n, x)	do { if (ubsadebug > (n)) printf x; } while (0)
#else
#define	DPRINTFN(n, x)
#endif
#define	DPRINTF(x) DPRINTFN(0, x)

#define	UBSA_MODVER		1	/* module version */

#define	UBSA_CONFIG_INDEX	1
#define	UBSA_IFACE_INDEX	0

#define	UBSA_INTR_INTERVAL	100	/* ms */

#define	UBSA_SET_BAUDRATE  	0x00
#define	UBSA_SET_STOP_BITS	0x01
#define	UBSA_SET_DATA_BITS	0x02
#define	UBSA_SET_PARITY		0x03
#define	UBSA_SET_DTR		0x0A
#define	UBSA_SET_RTS		0x0B
#define	UBSA_SET_BREAK		0x0C
#define	UBSA_SET_FLOW_CTRL	0x10

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

struct	ubsa_softc {
	struct device		 sc_dev;	/* base device */
	struct usbd_device	*sc_udev;	/* USB device */
	struct usbd_interface	*sc_iface;	/* interface */

	int			 sc_iface_number;	/* interface number */

	int			 sc_intr_number;	/* interrupt number */
	struct usbd_pipe	*sc_intr_pipe;	/* interrupt pipe */
	u_char			*sc_intr_buf;	/* interrupt buffer */
	int			 sc_isize;

	u_char			 sc_dtr;	/* current DTR state */
	u_char			 sc_rts;	/* current RTS state */

	u_char			 sc_lsr;	/* Local status register */
	u_char			 sc_msr;	/* ubsa status register */

	struct device		*sc_subdev;	/* ucom device */

};

void ubsa_intr(struct usbd_xfer *, void *, usbd_status);

void ubsa_get_status(void *, int, u_char *, u_char *);
void ubsa_set(void *, int, int, int);
int  ubsa_param(void *, int, struct termios *);
int  ubsa_open(void *, int);
void ubsa_close(void *, int);

void ubsa_break(struct ubsa_softc *sc, int onoff);
int  ubsa_request(struct ubsa_softc *, u_int8_t, u_int16_t);
void ubsa_dtr(struct ubsa_softc *, int);
void ubsa_rts(struct ubsa_softc *, int);
void ubsa_baudrate(struct ubsa_softc *, speed_t);
void ubsa_parity(struct ubsa_softc *, tcflag_t);
void ubsa_databits(struct ubsa_softc *, tcflag_t);
void ubsa_stopbits(struct ubsa_softc *, tcflag_t);
void ubsa_flow(struct ubsa_softc *, tcflag_t, tcflag_t);

const struct ucom_methods ubsa_methods = {
	ubsa_get_status,
	ubsa_set,
	ubsa_param,
	NULL,
	ubsa_open,
	ubsa_close,
	NULL,
	NULL
};

const struct usb_devno ubsa_devs[] = {
	/* Axesstel MV100H */
	{ USB_VENDOR_AXESSTEL, USB_PRODUCT_AXESSTEL_DATAMODEM },
	/* BELKIN F5U103 */
	{ USB_VENDOR_BELKIN, USB_PRODUCT_BELKIN_F5U103 },
	/* BELKIN F5U120 */
	{ USB_VENDOR_BELKIN, USB_PRODUCT_BELKIN_F5U120 },
	/* GoHubs GO-COM232 , Belkin F5U003 */
	{ USB_VENDOR_ETEK, USB_PRODUCT_ETEK_1COM },
	/* GoHubs GO-COM232 */
	{ USB_VENDOR_GOHUBS, USB_PRODUCT_GOHUBS_GOCOM232 },
	/* Peracom */
	{ USB_VENDOR_PERACOM, USB_PRODUCT_PERACOM_SERIAL1 },
	/* ZTE Inc. CMDMA MSM modem */
	{ USB_VENDOR_ZTE, USB_PRODUCT_ZTE_CDMA_MSM },
	/* ZTE Inc. AC8700 */
	{ USB_VENDOR_ZTE, USB_PRODUCT_ZTE_AC8700 },
};

int ubsa_match(struct device *, void *, void *);
void ubsa_attach(struct device *, struct device *, void *);
int ubsa_detach(struct device *, int);

struct cfdriver ubsa_cd = {
	NULL, "ubsa", DV_DULL
};

const struct cfattach ubsa_ca = {
	sizeof(struct ubsa_softc), ubsa_match, ubsa_attach, ubsa_detach
};

int
ubsa_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface != NULL)
		return (UMATCH_NONE);

	return (usb_lookup(ubsa_devs, uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
ubsa_attach(struct device *parent, struct device *self, void *aux)
{
	struct ubsa_softc *sc = (struct ubsa_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_device *dev = uaa->device;
	usb_config_descriptor_t *cdesc;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	const char *devname = sc->sc_dev.dv_xname;
	usbd_status err;
	struct ucom_attach_args uca;
	int i;

	sc->sc_udev = dev;

	/*
	 * initialize rts, dtr variables to something
	 * different from boolean 0, 1
	 */
	sc->sc_dtr = -1;
	sc->sc_rts = -1;

	DPRINTF(("ubsa attach: sc = %p\n", sc));

	/* initialize endpoints */
	uca.bulkin = uca.bulkout = -1;
	sc->sc_intr_number = -1;
	sc->sc_intr_pipe = NULL;

	/* Move the device into the configured state. */
	err = usbd_set_config_index(dev, UBSA_CONFIG_INDEX, 1);
	if (err) {
		printf("%s: failed to set configuration: %s\n",
		    devname, usbd_errstr(err));
		usbd_deactivate(sc->sc_udev);
		goto error;
	}

	/* get the config descriptor */
	cdesc = usbd_get_config_descriptor(sc->sc_udev);

	if (cdesc == NULL) {
		printf("%s: failed to get configuration descriptor\n",
		    devname);
		usbd_deactivate(sc->sc_udev);
		goto error;
	}

	/* get the first interface */
	err = usbd_device2interface_handle(dev, UBSA_IFACE_INDEX,
	    &sc->sc_iface);
	if (err) {
		printf("%s: failed to get interface: %s\n",
			devname, usbd_errstr(err));
		usbd_deactivate(sc->sc_udev);
		goto error;
	}

	/* Find the endpoints */

	id = usbd_get_interface_descriptor(sc->sc_iface);
	sc->sc_iface_number = id->bInterfaceNumber;

	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor for %d\n",
			    sc->sc_dev.dv_xname, i);
			usbd_deactivate(sc->sc_udev);
			goto error;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->sc_intr_number = ed->bEndpointAddress;
			sc->sc_isize = UGETW(ed->wMaxPacketSize);
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			uca.bulkin = ed->bEndpointAddress;
			uca.ibufsize = UGETW(ed->wMaxPacketSize);
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			uca.bulkout = ed->bEndpointAddress;
			uca.obufsize = UGETW(ed->wMaxPacketSize);
		}
	}

	if (sc->sc_intr_number == -1) {
		printf("%s: Could not find interrupt in\n", devname);
		usbd_deactivate(sc->sc_udev);
		goto error;
	}

	if (uca.bulkin == -1) {
		printf("%s: Could not find data bulk in\n", devname);
		usbd_deactivate(sc->sc_udev);
		goto error;
	}

	if (uca.bulkout == -1) {
		printf("%s: Could not find data bulk out\n", devname);
		usbd_deactivate(sc->sc_udev);
		goto error;
	}

	uca.portno = UCOM_UNK_PORTNO;
	/* bulkin, bulkout set above */
	uca.ibufsizepad = uca.ibufsize;
	uca.opkthdrlen = 0;
	uca.device = dev;
	uca.iface = sc->sc_iface;
	uca.methods = &ubsa_methods;
	uca.arg = sc;
	uca.info = NULL;

	DPRINTF(("ubsa: in = 0x%x, out = 0x%x, intr = 0x%x\n",
	    uca.bulkin, uca.bulkout, sc->sc_intr_number));

	sc->sc_subdev = config_found_sm(self, &uca, ucomprint, ucomsubmatch);

error:
	return;
}

int
ubsa_detach(struct device *self, int flags)
{
	struct ubsa_softc *sc = (struct ubsa_softc *)self;
	int rv = 0;


	DPRINTF(("ubsa_detach: sc = %p\n", sc));

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

int
ubsa_request(struct ubsa_softc *sc, u_int8_t request, u_int16_t value)
{
	usb_device_request_t req;
	usbd_status err;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = request;
	USETW(req.wValue, value);
	USETW(req.wIndex, sc->sc_iface_number);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_udev, &req, 0);
	if (err && err != USBD_STALLED)
		printf("%s: ubsa_request: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
	return (err);
}

void
ubsa_dtr(struct ubsa_softc *sc, int onoff)
{

	DPRINTF(("ubsa_dtr: onoff = %d\n", onoff));

	if (sc->sc_dtr == onoff)
		return;
	sc->sc_dtr = onoff;

	ubsa_request(sc, UBSA_SET_DTR, onoff ? 1 : 0);
}

void
ubsa_rts(struct ubsa_softc *sc, int onoff)
{

	DPRINTF(("ubsa_rts: onoff = %d\n", onoff));

	if (sc->sc_rts == onoff)
		return;
	sc->sc_rts = onoff;

	ubsa_request(sc, UBSA_SET_RTS, onoff ? 1 : 0);
}

void
ubsa_break(struct ubsa_softc *sc, int onoff)
{

	DPRINTF(("ubsa_rts: onoff = %d\n", onoff));

	ubsa_request(sc, UBSA_SET_BREAK, onoff ? 1 : 0);
}

void
ubsa_set(void *addr, int portno, int reg, int onoff)
{
	struct ubsa_softc *sc;

	sc = addr;
	switch (reg) {
	case UCOM_SET_DTR:
		ubsa_dtr(sc, onoff);
		break;
	case UCOM_SET_RTS:
		ubsa_rts(sc, onoff);
		break;
	case UCOM_SET_BREAK:
		ubsa_break(sc, onoff);
		break;
	default:
		break;
	}
}

void
ubsa_baudrate(struct ubsa_softc *sc, speed_t speed)
{
	u_int16_t value = 0;

	DPRINTF(("ubsa_baudrate: speed = %d\n", speed));

	switch(speed) {
	case B0:
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
		value = B230400 / speed;
		break;
	default:
		DPRINTF(("%s: ubsa_param: unsupported baudrate, "
		    "forcing default of 9600\n",
		    sc->sc_dev.dv_xname));
		value = B230400 / B9600;
		break;
	}

	if (speed == B0) {
		ubsa_flow(sc, 0, 0);
		ubsa_dtr(sc, 0);
		ubsa_rts(sc, 0);
	} else
		ubsa_request(sc, UBSA_SET_BAUDRATE, value);
}

void
ubsa_parity(struct ubsa_softc *sc, tcflag_t cflag)
{
	int value;

	DPRINTF(("ubsa_parity: cflag = 0x%x\n", cflag));

	if (cflag & PARENB)
		value = (cflag & PARODD) ? UBSA_PARITY_ODD : UBSA_PARITY_EVEN;
	else
		value = UBSA_PARITY_NONE;

	ubsa_request(sc, UBSA_SET_PARITY, value);
}

void
ubsa_databits(struct ubsa_softc *sc, tcflag_t cflag)
{
	int value;

	DPRINTF(("ubsa_databits: cflag = 0x%x\n", cflag));

	switch (cflag & CSIZE) {
	case CS5: value = 0; break;
	case CS6: value = 1; break;
	case CS7: value = 2; break;
	case CS8: value = 3; break;
	default:
		DPRINTF(("%s: ubsa_param: unsupported databits requested, "
		    "forcing default of 8\n",
		    sc->sc_dev.dv_xname));
		value = 3;
	}

	ubsa_request(sc, UBSA_SET_DATA_BITS, value);
}

void
ubsa_stopbits(struct ubsa_softc *sc, tcflag_t cflag)
{
	int value;

	DPRINTF(("ubsa_stopbits: cflag = 0x%x\n", cflag));

	value = (cflag & CSTOPB) ? 1 : 0;

	ubsa_request(sc, UBSA_SET_STOP_BITS, value);
}

void
ubsa_flow(struct ubsa_softc *sc, tcflag_t cflag, tcflag_t iflag)
{
	int value;

	DPRINTF(("ubsa_flow: cflag = 0x%x, iflag = 0x%x\n", cflag, iflag));

	value = 0;
	if (cflag & CRTSCTS)
		value |= UBSA_FLOW_OCTS | UBSA_FLOW_IRTS;
	if (iflag & (IXON|IXOFF))
		value |= UBSA_FLOW_OXON | UBSA_FLOW_IXON;

	ubsa_request(sc, UBSA_SET_FLOW_CTRL, value);
}

int
ubsa_param(void *addr, int portno, struct termios *ti)
{
	struct ubsa_softc *sc = addr;

	DPRINTF(("ubsa_param: sc = %p\n", sc));

	ubsa_baudrate(sc, ti->c_ospeed);
	ubsa_parity(sc, ti->c_cflag);
	ubsa_databits(sc, ti->c_cflag);
	ubsa_stopbits(sc, ti->c_cflag);
	ubsa_flow(sc, ti->c_cflag, ti->c_iflag);

	return (0);
}

int
ubsa_open(void *addr, int portno)
{
	struct ubsa_softc *sc = addr;
	int err;

	if (usbd_is_dying(sc->sc_udev))
		return (ENXIO);

	DPRINTF(("ubsa_open: sc = %p\n", sc));

	if (sc->sc_intr_number != -1 && sc->sc_intr_pipe == NULL) {
		sc->sc_intr_buf = malloc(sc->sc_isize, M_USBDEV, M_WAITOK);
		err = usbd_open_pipe_intr(sc->sc_iface,
		    sc->sc_intr_number,
		    USBD_SHORT_XFER_OK,
		    &sc->sc_intr_pipe,
		    sc,
		    sc->sc_intr_buf,
		    sc->sc_isize,
		    ubsa_intr,
		    UBSA_INTR_INTERVAL);
		if (err) {
			printf("%s: cannot open interrupt pipe (addr %d)\n",
			    sc->sc_dev.dv_xname,
			    sc->sc_intr_number);
			return (EIO);
		}
	}

	return (0);
}

void
ubsa_close(void *addr, int portno)
{
	struct ubsa_softc *sc = addr;
	int err;

	if (usbd_is_dying(sc->sc_udev))
		return;

	DPRINTF(("ubsa_close: close\n"));

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
ubsa_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct ubsa_softc *sc = priv;
	u_char *buf;
	struct usb_cdc_notification *cdcbuf;

	buf = sc->sc_intr_buf;
	cdcbuf = (struct usb_cdc_notification *)sc->sc_intr_buf;
	if (usbd_is_dying(sc->sc_udev))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		DPRINTF(("%s: ubsa_intr: abnormal status: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(status)));
		usbd_clear_endpoint_stall_async(sc->sc_intr_pipe);
		return;
	}

#if 1 /* test */
	if (cdcbuf->bmRequestType == UCDC_NOTIFICATION) {
		printf("%s: this device is using CDC notify message in" 
		    " intr pipe.\n"
		    "Please send your dmesg to <bugs@openbsd.org>, thanks.\n",
		    sc->sc_dev.dv_xname);
		printf("%s: intr buffer 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", 
		    sc->sc_dev.dv_xname,
		    buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

		/* check the buffer data */
		if (cdcbuf->bNotification == UCDC_N_SERIAL_STATE)
			printf("%s:notify serial state, len=%d, data=0x%02x\n",
			    sc->sc_dev.dv_xname,
			    UGETW(cdcbuf->wLength), cdcbuf->data[0]);
	}
#endif

	/* incidentally, Belkin adapter status bits match UART 16550 bits */
	sc->sc_lsr = buf[2];
	sc->sc_msr = buf[3];

	DPRINTF(("%s: ubsa lsr = 0x%02x, msr = 0x%02x\n",
	    sc->sc_dev.dv_xname, sc->sc_lsr, sc->sc_msr));

	ucom_status_change((struct ucom_softc *)sc->sc_subdev);
}

void
ubsa_get_status(void *addr, int portno, u_char *lsr, u_char *msr)
{
	struct ubsa_softc *sc = addr;

	DPRINTF(("ubsa_get_status\n"));

	if (lsr != NULL)
		*lsr = sc->sc_lsr;
	if (msr != NULL)
		*msr = sc->sc_msr;
}
