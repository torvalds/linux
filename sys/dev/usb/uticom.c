/*	$OpenBSD: uticom.c,v 1.36 2024/05/23 03:21:09 jsg Exp $	*/
/*
 * Copyright (c) 2005 Dmitry Komissaroff <dxi@mail.ru>.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/tty.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/usbcdc.h>

#include <dev/usb/ucomvar.h>

#ifdef UTICOM_DEBUG
static int uticomdebug = 0;
#define DPRINTFN(n, x)	do { if (uticomdebug > (n)) printf x; } while (0)
#else
#define DPRINTFN(n, x)
#endif

#define DPRINTF(x) DPRINTFN(0, x)

#define	UTICOM_CONFIG_INDEX	1
#define	UTICOM_ACTIVE_INDEX	2

#define	UTICOM_IFACE_INDEX	0

/*
 * These are the maximum number of bytes transferred per frame.
 * The output buffer size cannot be increased due to the size encoding.
 */
#define UTICOM_IBUFSZ		64
#define UTICOM_OBUFSZ		64

#define UTICOM_FW_BUFSZ		16284

#define UTICOM_INTR_INTERVAL	100	/* ms */

#define UTICOM_RQ_LINE		0
/* Used to sync data0/1-toggle on reopen bulk pipe. */
#define UTICOM_RQ_SOF		1
#define UTICOM_RQ_SON		2

#define UTICOM_RQ_BAUD		3
#define UTICOM_RQ_LCR		4
#define UTICOM_RQ_FCR		5
#define UTICOM_RQ_RTS		6
#define UTICOM_RQ_DTR		7
#define UTICOM_RQ_BREAK		8
#define UTICOM_RQ_CRTSCTS	9

#define UTICOM_BRATE_REF	923077

#define UTICOM_SET_DATA_BITS(x)	(x - 5)

#define UTICOM_STOP_BITS_1	0x00
#define UTICOM_STOP_BITS_2	0x40

#define UTICOM_PARITY_NONE	0x00
#define UTICOM_PARITY_ODD	0x08
#define UTICOM_PARITY_EVEN	0x18

#define UTICOM_LCR_OVR		0x1
#define UTICOM_LCR_PTE		0x2
#define UTICOM_LCR_FRE		0x4
#define UTICOM_LCR_BRK		0x8

#define UTICOM_MCR_CTS		0x1
#define UTICOM_MCR_DSR		0x2
#define UTICOM_MCR_CD		0x4
#define UTICOM_MCR_RI		0x8

/* Structures */
struct uticom_fw_header {
	uint16_t	length;
	uint8_t		checkSum;
} __packed;

struct uticom_buf {
	unsigned int		buf_size;
	char			*buf_buf;
	char			*buf_get;
	char			*buf_put;
};

struct	uticom_softc {
	struct device		 sc_dev;	/* base device */
	struct usbd_device	*sc_udev;	/* device */
	struct usbd_interface	*sc_iface;	/* interface */

	struct usbd_interface	*sc_intr_iface;	/* interrupt interface */
	int			sc_intr_number;	/* interrupt number */
	struct usbd_pipe	*sc_intr_pipe;	/* interrupt pipe */
	u_char			*sc_intr_buf;	/* interrupt buffer */
	int			sc_isize;

	u_char			sc_dtr;		/* current DTR state */
	u_char			sc_rts;		/* current RTS state */
	u_char			sc_status;

	u_char			sc_lsr;		/* Local status register */
	u_char			sc_msr;		/* uticom status register */

	struct device		*sc_subdev;
};

static	usbd_status uticom_reset(struct uticom_softc *);
static	usbd_status uticom_set_crtscts(struct uticom_softc *);
static	void uticom_intr(struct usbd_xfer *, void *, usbd_status);

static	void uticom_set(void *, int, int, int);
static	void uticom_dtr(struct uticom_softc *, int);
static	void uticom_rts(struct uticom_softc *, int);
static	void uticom_break(struct uticom_softc *, int);
static	void uticom_get_status(void *, int, u_char *, u_char *);
static	int  uticom_param(void *, int, struct termios *);
static	int  uticom_open(void *, int);
static	void uticom_close(void *, int);

void uticom_attach_hook(struct device *);

static int uticom_download_fw(struct uticom_softc *sc, int pipeno,
    struct usbd_device *dev);

const struct ucom_methods uticom_methods = {
	uticom_get_status,
	uticom_set,
	uticom_param,
	NULL,
	uticom_open,
	uticom_close,
	NULL,
	NULL
};

int	uticom_match(struct device *, void *, void *);
void	uticom_attach(struct device *, struct device *, void *);
int	uticom_detach(struct device *, int);

struct cfdriver uticom_cd = {
	NULL, "uticom", DV_DULL
};

const struct cfattach uticom_ca = {
	sizeof(struct uticom_softc), uticom_match, uticom_attach, uticom_detach
};

static const struct usb_devno uticom_devs[] = {
	{ USB_VENDOR_TI, USB_PRODUCT_TI_TUSB3410 },
	{ USB_VENDOR_TI, USB_PRODUCT_TI_MSP430_JTAG },
	{ USB_VENDOR_STARTECH, USB_PRODUCT_STARTECH_ICUSB232X },
	{ USB_VENDOR_MOXA, USB_PRODUCT_MOXA_UPORT1110 },
	{ USB_VENDOR_ABBOTT, USB_PRODUCT_ABBOTT_STEREO_PLUG }
};

int
uticom_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface != NULL)
		return (UMATCH_NONE);

	return (usb_lookup(uticom_devs, uaa->vendor, uaa->product) != NULL ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
uticom_attach(struct device *parent, struct device *self, void *aux)
{
	struct uticom_softc	*sc = (struct uticom_softc *)self;
	struct usb_attach_arg	*uaa = aux;
	struct usbd_device	*dev = uaa->device;

	sc->sc_udev = dev;
	sc->sc_iface = uaa->iface;

	config_mountroot(self, uticom_attach_hook);
}

void
uticom_attach_hook(struct device *self)
{
	struct uticom_softc		*sc = (struct uticom_softc *)self;
	usb_config_descriptor_t		*cdesc;
	usb_interface_descriptor_t	*id;
	usb_endpoint_descriptor_t	*ed;
	usbd_status			 err;
	int				 status, i;
	usb_device_descriptor_t		*dd;
	struct ucom_attach_args		 uca;

	/* Initialize endpoints. */
	uca.bulkin = uca.bulkout = -1;
	sc->sc_intr_number = -1;
	sc->sc_intr_pipe = NULL;

	dd = usbd_get_device_descriptor(sc->sc_udev);
	DPRINTF(("%s: uticom_attach: num of configurations %d\n",
	    sc->sc_dev.dv_xname, dd->bNumConfigurations));

	/* The device without firmware has single configuration with single
	 * bulk out interface. */
	if (dd->bNumConfigurations > 1)
		goto fwload_done;

	/* Loading firmware. */
	DPRINTF(("%s: uticom_attach: starting loading firmware\n",
	    sc->sc_dev.dv_xname));

	err = usbd_set_config_index(sc->sc_udev, UTICOM_CONFIG_INDEX, 1);
	if (err) {
		printf("%s: failed to set configuration: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		usbd_deactivate(sc->sc_udev);
		return;
	}

	/* Get the config descriptor. */
	cdesc = usbd_get_config_descriptor(sc->sc_udev);

	if (cdesc == NULL) {
		printf("%s: failed to get configuration descriptor\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	err = usbd_device2interface_handle(sc->sc_udev, UTICOM_IFACE_INDEX,
	    &sc->sc_iface);
	if (err) {
		printf("%s: failed to get interface: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		usbd_deactivate(sc->sc_udev);
		return;
	}

	/* Find the bulk out interface used to upload firmware. */
	id = usbd_get_interface_descriptor(sc->sc_iface);

	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor for %d\n",
			    sc->sc_dev.dv_xname, i);
			usbd_deactivate(sc->sc_udev);
			return;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			uca.bulkout = ed->bEndpointAddress;
			DPRINTF(("%s: uticom_attach: data bulk out num: %d\n",
			    sc->sc_dev.dv_xname, ed->bEndpointAddress));
		}

		if (uca.bulkout == -1) {
			printf("%s: could not find data bulk out\n",
			    sc->sc_dev.dv_xname);
			usbd_deactivate(sc->sc_udev);
			return;
		}
	}

	status = uticom_download_fw(sc, uca.bulkout, sc->sc_udev);

	if (status) {
		printf("%s: firmware download failed\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	} else {
		DPRINTF(("%s: firmware download succeeded\n",
		    sc->sc_dev.dv_xname));
	}

	status = usbd_reload_device_desc(sc->sc_udev);
	if (status) {
		printf("%s: error reloading device descriptor\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

fwload_done:
	dd = usbd_get_device_descriptor(sc->sc_udev);
	DPRINTF(("%s: uticom_attach: num of configurations %d\n",
	    sc->sc_dev.dv_xname, dd->bNumConfigurations));

	err = usbd_set_config_index(sc->sc_udev, UTICOM_ACTIVE_INDEX, 1);
	if (err) {
		printf("%s: failed to set configuration: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		usbd_deactivate(sc->sc_udev);
		return;
	}

	/* Get the config descriptor. */
	cdesc = usbd_get_config_descriptor(sc->sc_udev);
	if (cdesc == NULL) {
		printf("%s: failed to get configuration descriptor\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	/* Get the interface (XXX: multiport chips are not supported yet). */
	err = usbd_device2interface_handle(sc->sc_udev, UTICOM_IFACE_INDEX,
	    &sc->sc_iface);
	if (err) {
		printf("%s: failed to get interface: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		usbd_deactivate(sc->sc_udev);
		return;
	}

	/* Find the interrupt endpoints. */
	id = usbd_get_interface_descriptor(sc->sc_iface);

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

	if (sc->sc_intr_number == -1) {
		printf("%s: could not find interrupt in\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	/* Keep interface for interrupt. */
	sc->sc_intr_iface = sc->sc_iface;

	/* Find the bulk{in,out} endpoints. */
	id = usbd_get_interface_descriptor(sc->sc_iface);

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
		printf("%s: could not find data bulk in\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	if (uca.bulkout == -1) {
		printf("%s: could not find data bulk out\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	sc->sc_dtr = sc->sc_rts = -1;

	uca.portno = UCOM_UNK_PORTNO;
	uca.ibufsize = UTICOM_IBUFSZ;
	uca.obufsize = UTICOM_OBUFSZ;
	uca.ibufsizepad = UTICOM_IBUFSZ;
	uca.device = sc->sc_udev;
	uca.iface = sc->sc_iface;
	uca.opkthdrlen = 0;
	uca.methods = &uticom_methods;
	uca.arg = sc;
	uca.info = NULL;

	err = uticom_reset(sc);
	if (err) {
		printf("%s: reset failed: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		usbd_deactivate(sc->sc_udev);
		return;
	}

	DPRINTF(("%s: uticom_attach: in = 0x%x, out = 0x%x, intr = 0x%x\n",
	    sc->sc_dev.dv_xname, uca.bulkin,
	    uca.bulkout, sc->sc_intr_number));

	sc->sc_subdev = config_found_sm((struct device *)sc, &uca, ucomprint, ucomsubmatch);
}

int
uticom_detach(struct device *self, int flags)
{
	struct uticom_softc *sc = (struct uticom_softc *)self;

	DPRINTF(("%s: uticom_detach: sc = %p\n",
	    sc->sc_dev.dv_xname, sc));

	if (sc->sc_subdev != NULL) {
		config_detach(sc->sc_subdev, flags);
		sc->sc_subdev = NULL;
	}

	if (sc->sc_intr_pipe != NULL) {
		usbd_close_pipe(sc->sc_intr_pipe);
		free(sc->sc_intr_buf, M_USBDEV, sc->sc_isize);
		sc->sc_intr_pipe = NULL;
	}

	return (0);
}

static usbd_status
uticom_reset(struct uticom_softc *sc)
{
	usb_device_request_t req;
	usbd_status err;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_SON;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err){
		printf("%s: uticom_reset: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return (EIO);
	}

	DPRINTF(("%s: uticom_reset: done\n", sc->sc_dev.dv_xname));
	return (0);
}

static void
uticom_set(void *addr, int portno, int reg, int onoff)
{
	struct uticom_softc *sc = addr;

	switch (reg) {
	case UCOM_SET_DTR:
		uticom_dtr(sc, onoff);
		break;
	case UCOM_SET_RTS:
		uticom_rts(sc, onoff);
		break;
	case UCOM_SET_BREAK:
		uticom_break(sc, onoff);
		break;
	default:
		break;
	}
}

static void
uticom_dtr(struct uticom_softc *sc, int onoff)
{
	usb_device_request_t req;
	usbd_status err;

	DPRINTF(("%s: uticom_dtr: onoff = %d\n", sc->sc_dev.dv_xname,
	    onoff));

	if (sc->sc_dtr == onoff)
		return;
	sc->sc_dtr = onoff;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_DTR;
	USETW(req.wValue, sc->sc_dtr ? UCDC_LINE_DTR : 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err)
		printf("%s: uticom_dtr: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
}

static void
uticom_rts(struct uticom_softc *sc, int onoff)
{
	usb_device_request_t req;
	usbd_status err;

	DPRINTF(("%s: uticom_rts: onoff = %d\n", sc->sc_dev.dv_xname,
	    onoff));

	if (sc->sc_rts == onoff)
		return;
	sc->sc_rts = onoff;
	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_RTS;
	USETW(req.wValue, sc->sc_rts ? UCDC_LINE_RTS : 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err)
		printf("%s: uticom_rts: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
}

static void
uticom_break(struct uticom_softc *sc, int onoff)
{
	usb_device_request_t req;
	usbd_status err;

	DPRINTF(("%s: uticom_break: onoff = %d\n", sc->sc_dev.dv_xname,
	    onoff));

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_BREAK;
	USETW(req.wValue, onoff ? 1 : 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err)
		printf("%s: uticom_break: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
}

static usbd_status
uticom_set_crtscts(struct uticom_softc *sc)
{
	usb_device_request_t req;
	usbd_status err;

	DPRINTF(("%s: uticom_set_crtscts: on\n", sc->sc_dev.dv_xname));

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_CRTSCTS;
	USETW(req.wValue, 1);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err) {
		printf("%s: uticom_set_crtscts: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return (err);
	}

	return (USBD_NORMAL_COMPLETION);
}

static int
uticom_param(void *vsc, int portno, struct termios *t)
{
	struct uticom_softc *sc = (struct uticom_softc *)vsc;
	usb_device_request_t req;
	usbd_status err;
	uint8_t data;

	DPRINTF(("%s: uticom_param\n", sc->sc_dev.dv_xname));

	switch (t->c_ospeed) {
	case 1200:
	case 2400:
	case 4800:
	case 7200:
	case 9600:
	case 14400:
	case 19200:
	case 38400:
	case 57600:
	case 115200:
	case 230400:
	case 460800:
	case 921600:
		req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
		req.bRequest = UTICOM_RQ_BAUD;
		USETW(req.wValue, (UTICOM_BRATE_REF / t->c_ospeed));
		USETW(req.wIndex, 0);
		USETW(req.wLength, 0);

		err = usbd_do_request(sc->sc_udev, &req, 0);
		if (err) {
			printf("%s: uticom_param: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(err));
			return (EIO);
		}
		break;
	default:
		printf("%s: uticom_param: unsupported baud rate %d\n",
		    sc->sc_dev.dv_xname, t->c_ospeed);
		return (EINVAL);
	}

	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
		data = UTICOM_SET_DATA_BITS(5);
		break;
	case CS6:
		data = UTICOM_SET_DATA_BITS(6);
		break;
	case CS7:
		data = UTICOM_SET_DATA_BITS(7);
		break;
	case CS8:
		data = UTICOM_SET_DATA_BITS(8);
		break;
	default:
		return (EIO);
	}

	if (ISSET(t->c_cflag, CSTOPB))
		data |= UTICOM_STOP_BITS_2;
	else
		data |= UTICOM_STOP_BITS_1;

	if (ISSET(t->c_cflag, PARENB)) {
		if (ISSET(t->c_cflag, PARODD))
			data |= UTICOM_PARITY_ODD;
		else
			data |= UTICOM_PARITY_EVEN;
	} else
		data |= UTICOM_PARITY_NONE;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_LCR;
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	USETW(req.wValue, data);

	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err) {
		printf("%s: uticom_param: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return (err);
	}

	if (ISSET(t->c_cflag, CRTSCTS)) {
		err = uticom_set_crtscts(sc);
		if (err)
			return (EIO);
	}

	return (0);
}

static int
uticom_open(void *addr, int portno)
{
	struct uticom_softc *sc = addr;
	usbd_status err;

	if (usbd_is_dying(sc->sc_udev))
		return (ENXIO);

	DPRINTF(("%s: uticom_open\n", sc->sc_dev.dv_xname));

	sc->sc_status = 0; /* clear status bit */

	if (sc->sc_intr_number != -1 && sc->sc_intr_pipe == NULL) {
		sc->sc_intr_buf = malloc(sc->sc_isize, M_USBDEV, M_WAITOK);
		err = usbd_open_pipe_intr(sc->sc_intr_iface, sc->sc_intr_number,
		    USBD_SHORT_XFER_OK, &sc->sc_intr_pipe, sc, sc->sc_intr_buf,
		    sc->sc_isize, uticom_intr, UTICOM_INTR_INTERVAL);
		if (err) {
			printf("%s: cannot open interrupt pipe (addr %d)\n",
			    sc->sc_dev.dv_xname, sc->sc_intr_number);
			return (EIO);
		}
	}

	DPRINTF(("%s: uticom_open: port opened\n", sc->sc_dev.dv_xname));
	return (0);
}

static void
uticom_close(void *addr, int portno)
{
	struct uticom_softc *sc = addr;
	usb_device_request_t req;
	usbd_status err;

	if (usbd_is_dying(sc->sc_udev))
		return;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UTICOM_RQ_SON;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	/* Try to reset UART part of chip. */
	err = usbd_do_request(sc->sc_udev, &req, NULL);
	if (err) {
		printf("%s: uticom_close: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));
		return;
	}

	DPRINTF(("%s: uticom_close: close\n", sc->sc_dev.dv_xname));

	if (sc->sc_intr_pipe != NULL) {
		err = usbd_close_pipe(sc->sc_intr_pipe);
		if (err)
			printf("%s: close interrupt pipe failed: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(err));
		free(sc->sc_intr_buf, M_USBDEV, sc->sc_isize);
		sc->sc_intr_pipe = NULL;
	}
}

static void
uticom_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct uticom_softc *sc = priv;
	u_char *buf = sc->sc_intr_buf;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			DPRINTF(("%s: uticom_intr: int status: %s\n",
			    sc->sc_dev.dv_xname, usbd_errstr(status)));
			return;
		}

		DPRINTF(("%s: uticom_intr: abnormal status: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(status)));
		usbd_clear_endpoint_stall_async(sc->sc_intr_pipe);
		return;
	}

	if (!xfer->actlen)
		return;

	DPRINTF(("%s: xfer_length = %d\n", sc->sc_dev.dv_xname,
	    xfer->actlen));

	sc->sc_lsr = sc->sc_msr = 0;

	if (buf[0] == 0) {
		/* msr registers */
		if (buf[1] & UTICOM_MCR_CTS)
			sc->sc_msr |= UMSR_CTS;
		if (buf[1] & UTICOM_MCR_DSR)
			sc->sc_msr |= UMSR_DSR;
		if (buf[1] & UTICOM_MCR_CD)
			sc->sc_msr |= UMSR_DCD;
		if (buf[1] & UTICOM_MCR_RI)
			sc->sc_msr |= UMSR_RI;
	} else {
		/* lsr registers */
		if (buf[0] & UTICOM_LCR_OVR)
			sc->sc_lsr |= ULSR_OE;
		if (buf[0] & UTICOM_LCR_PTE)
			sc->sc_lsr |= ULSR_PE;
		if (buf[0] & UTICOM_LCR_FRE)
			sc->sc_lsr |= ULSR_FE;
		if (buf[0] & UTICOM_LCR_BRK)
			sc->sc_lsr |= ULSR_BI;
	}

//	if (uticomstickdsr)
//		sc->sc_msr |= UMSR_DSR;

	ucom_status_change((struct ucom_softc *)sc->sc_subdev);
}

static void
uticom_get_status(void *addr, int portno, u_char *lsr, u_char *msr)
{
#if 0 /* TODO */
	struct uticom_softc *sc = addr;

	DPRINTF(("uticom_get_status:\n"));

	if (lsr != NULL)
		*lsr = sc->sc_lsr;
	if (msr != NULL)
		*msr = sc->sc_msr;
#endif
	return;
}

static int
uticom_download_fw(struct uticom_softc *sc, int pipeno,
    struct usbd_device *dev)
{
	u_char *obuf, *firmware;
	size_t firmware_size;
	int buffer_size, pos;
	uint8_t cs = 0, *buffer;
	usbd_status err;
	struct uticom_fw_header *header;
	struct usbd_xfer *oxfer = 0;
	usbd_status error = 0;
	struct usbd_pipe *pipe;

	error = loadfirmware("tusb3410", &firmware, &firmware_size);
	if (error)
		return (error);

	buffer_size = UTICOM_FW_BUFSZ + sizeof(struct uticom_fw_header);
	buffer = malloc(buffer_size, M_USBDEV, M_WAITOK | M_CANFAIL);

	if (!buffer) {
		printf("%s: uticom_download_fw: out of memory\n",
		    sc->sc_dev.dv_xname);
		free(firmware, M_DEVBUF, firmware_size);
		return ENOMEM;
	}

	memcpy(buffer, firmware, firmware_size);
	memset(buffer + firmware_size, 0xff, buffer_size - firmware_size);

	for (pos = sizeof(struct uticom_fw_header); pos < buffer_size; pos++)
		cs = (uint8_t)(cs + buffer[pos]);

	header = (struct uticom_fw_header*)buffer;
	header->length = (uint16_t)(buffer_size -
	    sizeof(struct uticom_fw_header));
	header->checkSum = cs;

	DPRINTF(("%s: downloading firmware ...\n",
	    sc->sc_dev.dv_xname));

	err = usbd_open_pipe(sc->sc_iface, pipeno, USBD_EXCLUSIVE_USE,
	    &pipe);
	if (err) {
		printf("%s: open bulk out error (addr %d): %s\n",
		    sc->sc_dev.dv_xname, pipeno, usbd_errstr(err));
		error = EIO;
		goto finish;
	}

	oxfer = usbd_alloc_xfer(dev);
	if (oxfer == NULL) {
		error = ENOMEM;
		goto finish;
	}

	obuf = usbd_alloc_buffer(oxfer, buffer_size);
	if (obuf == NULL) {
		error = ENOMEM;
		goto finish;
	}

	memcpy(obuf, buffer, buffer_size);

	usbd_setup_xfer(oxfer, pipe, (void *)sc, obuf, buffer_size,
	    USBD_NO_COPY | USBD_SYNCHRONOUS, USBD_NO_TIMEOUT, 0);
	err = usbd_transfer(oxfer);

	if (err != USBD_NORMAL_COMPLETION)
		printf("%s: uticom_download_fw: error: %s\n",
		    sc->sc_dev.dv_xname, usbd_errstr(err));

finish:
	free(firmware, M_DEVBUF, firmware_size);
	usbd_free_buffer(oxfer);
	usbd_free_xfer(oxfer);
	oxfer = NULL;
	usbd_close_pipe(pipe);
	free(buffer, M_USBDEV, buffer_size);
	return err;
}
