/*	$OpenBSD: uvisor.c,v 1.54 2024/05/23 03:21:09 jsg Exp $	*/
/*	$NetBSD: uvisor.c,v 1.21 2003/08/03 21:59:26 nathanw Exp $	*/

/*
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * Handspring Visor (Palmpilot compatible PDA) driver
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/tty.h>

#include <dev/usb/usb.h>

#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/ucomvar.h>

#ifdef UVISOR_DEBUG
#define DPRINTF(x)	if (uvisordebug) printf x
#define DPRINTFN(n,x)	if (uvisordebug>(n)) printf x
int uvisordebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define UVISOR_IFACE_INDEX	0

/* From the Linux driver */
/*
 * UVISOR_REQUEST_BYTES_AVAILABLE asks the visor for the number of bytes that
 * are available to be transferred to the host for the specified endpoint.
 * Currently this is not used, and always returns 0x0001
 */
#define UVISOR_REQUEST_BYTES_AVAILABLE		0x01

/*
 * UVISOR_CLOSE_NOTIFICATION is set to the device to notify it that the host
 * is now closing the pipe. An empty packet is sent in response.
 */
#define UVISOR_CLOSE_NOTIFICATION		0x02

/*
 * UVISOR_GET_CONNECTION_INFORMATION is sent by the host during enumeration to
 * get the endpoints used by the connection.
 */
#define UVISOR_GET_CONNECTION_INFORMATION	0x03


/*
 * UVISOR_GET_CONNECTION_INFORMATION returns data in the following format
 */
#define UVISOR_MAX_CONN 8
struct uvisor_connection_info {
	uWord	num_ports;
	struct {
		uByte	port_function_id;
		uByte	port;
	} connections[UVISOR_MAX_CONN];
};
#define UVISOR_CONNECTION_INFO_SIZE 18

/* struct uvisor_connection_info.connection[x].port_function_id defines: */
#define UVISOR_FUNCTION_GENERIC		0x00
#define UVISOR_FUNCTION_DEBUGGER	0x01
#define UVISOR_FUNCTION_HOTSYNC		0x02
#define UVISOR_FUNCTION_CONSOLE		0x03
#define UVISOR_FUNCTION_REMOTE_FILE_SYS	0x04

/*
 * Unknown PalmOS stuff.
 */
#define UVISOR_GET_PALM_INFORMATION		0x04
#define UVISOR_GET_PALM_INFORMATION_LEN		0x44

struct uvisor_palm_connection_info {
	uByte	num_ports;
	uByte	endpoint_numbers_different;
	uWord	reserved1;
	struct {
		uDWord	port_function_id;
		uByte	port;
		uByte	end_point_info;
		uWord	reserved;
	} connections[UVISOR_MAX_CONN];
};

#define UVISORIBUFSIZE 64
#define UVISOROBUFSIZE 1024

struct uvisor_softc {
	struct device		sc_dev;		/* base device */
	struct usbd_device	*sc_udev;	/* device */
	struct usbd_interface	*sc_iface;	/* interface */
/* 
 * added sc_vendor for later interrogation in failed initialisations
 */
	int			sc_vendor;	/* USB device vendor */

	struct device		*sc_subdevs[UVISOR_MAX_CONN];
	int			sc_numcon;

	u_int16_t		sc_flags;
};

usbd_status uvisor_init(struct uvisor_softc *,
			       struct uvisor_connection_info *,
			       struct uvisor_palm_connection_info *);

void uvisor_close(void *, int);


const struct ucom_methods uvisor_methods = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	uvisor_close,
	NULL,
	NULL,
};

struct uvisor_type {
	struct usb_devno	uv_dev;
	u_int16_t		uv_flags;	
#define PALM4	0x0001
#define VISOR	0x0002
#define NOFRE	0x0004
#define CLIE4	(VISOR|NOFRE)
};
static const struct uvisor_type uvisor_devs[] = {
	{{ USB_VENDOR_ACEECA, USB_PRODUCT_ACEECA_MEZ1000 }, PALM4 },
	{{ USB_VENDOR_FOSSIL, USB_PRODUCT_FOSSIL_WRISTPDA }, PALM4 },
	{{ USB_VENDOR_GARMIN, USB_PRODUCT_GARMIN_IQUE3600 }, PALM4 },
	{{ USB_VENDOR_HANDSPRING, USB_PRODUCT_HANDSPRING_VISOR }, VISOR },
	{{ USB_VENDOR_HANDSPRING, USB_PRODUCT_HANDSPRING_TREO }, PALM4 },
	{{ USB_VENDOR_HANDSPRING, USB_PRODUCT_HANDSPRING_TREO600 }, VISOR },
	{{ USB_VENDOR_PALM, USB_PRODUCT_PALM_M500 }, PALM4 },
	{{ USB_VENDOR_PALM, USB_PRODUCT_PALM_M505 }, PALM4 },
	{{ USB_VENDOR_PALM, USB_PRODUCT_PALM_M515 }, PALM4 },
	{{ USB_VENDOR_PALM, USB_PRODUCT_PALM_I705 }, PALM4 },
	{{ USB_VENDOR_PALM, USB_PRODUCT_PALM_M125 }, PALM4 },
	{{ USB_VENDOR_PALM, USB_PRODUCT_PALM_M130 }, PALM4 },
	{{ USB_VENDOR_PALM, USB_PRODUCT_PALM_TUNGSTEN_Z }, PALM4 },
	{{ USB_VENDOR_PALM, USB_PRODUCT_PALM_TUNGSTEN_T }, PALM4 },
	{{ USB_VENDOR_PALM, USB_PRODUCT_PALM_ZIRE }, PALM4 },
	{{ USB_VENDOR_PALM, USB_PRODUCT_PALM_ZIRE_31 }, PALM4 },
	{{ USB_VENDOR_SONY, USB_PRODUCT_SONY_CLIE_40 }, PALM4 },
	{{ USB_VENDOR_SONY, USB_PRODUCT_SONY_CLIE_41 }, PALM4 },
	{{ USB_VENDOR_SONY, USB_PRODUCT_SONY_CLIE_S360 }, PALM4 },
	{{ USB_VENDOR_SONY, USB_PRODUCT_SONY_CLIE_NX60 }, PALM4 },
	{{ USB_VENDOR_SONY, USB_PRODUCT_SONY_CLIE_TJ25 }, PALM4 },
/*	{{ USB_VENDOR_SONY, USB_PRODUCT_SONY_CLIE_25 }, PALM4 },*/
	{{ USB_VENDOR_TAPWAVE, USB_PRODUCT_TAPWAVE_ZODIAC }, PALM4 },
};
#define uvisor_lookup(v, p) ((struct uvisor_type *)usb_lookup(uvisor_devs, v, p))

int uvisor_match(struct device *, void *, void *);
void uvisor_attach(struct device *, struct device *, void *);
int uvisor_detach(struct device *, int);

struct cfdriver uvisor_cd = {
	NULL, "uvisor", DV_DULL
};

const struct cfattach uvisor_ca = {
	sizeof(struct uvisor_softc), uvisor_match, uvisor_attach, uvisor_detach
};

int
uvisor_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	DPRINTFN(20,("uvisor: vendor=0x%x, product=0x%x\n",
		     uaa->vendor, uaa->product));

	return (uvisor_lookup(uaa->vendor, uaa->product) != NULL ?
		UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

void
uvisor_attach(struct device *parent, struct device *self, void *aux)
{
	struct uvisor_softc *sc = (struct uvisor_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_device *dev = uaa->device;
	struct usbd_interface *iface;
	usb_interface_descriptor_t *id;
	struct uvisor_connection_info coninfo;
	struct uvisor_palm_connection_info palmconinfo;
	usb_endpoint_descriptor_t *ed;
	int i, j, hasin, hasout, port;
	usbd_status err;
	struct ucom_attach_args uca;

	DPRINTFN(10,("\nuvisor_attach: sc=%p\n", sc));

	err = usbd_device2interface_handle(dev, UVISOR_IFACE_INDEX, &iface);
	if (err) {
		printf(": failed to get interface, err=%s\n",
		    usbd_errstr(err));
		goto bad;
	}

	sc->sc_flags = uvisor_lookup(uaa->vendor, uaa->product)->uv_flags;
	sc->sc_vendor = uaa->vendor;
	
	if ((sc->sc_flags & (VISOR | PALM4)) == 0) {
		printf("%s: device is neither visor nor palm\n", 
		    sc->sc_dev.dv_xname);
		goto bad;
	}

	id = usbd_get_interface_descriptor(iface);

	sc->sc_udev = dev;
	sc->sc_iface = iface;

	uca.ibufsize = UVISORIBUFSIZE;
	uca.obufsize = UVISOROBUFSIZE;
	uca.ibufsizepad = UVISORIBUFSIZE;
	uca.opkthdrlen = 0;
	uca.device = dev;
	uca.iface = iface;
	uca.methods = &uvisor_methods;
	uca.arg = sc;

	err = uvisor_init(sc, &coninfo, &palmconinfo);
	if (err) {
		printf("%s: init failed, %s\n", sc->sc_dev.dv_xname,
		       usbd_errstr(err));
		goto bad;
	}

	if (sc->sc_flags & VISOR) {
		sc->sc_numcon = UGETW(coninfo.num_ports);
		if (sc->sc_numcon > UVISOR_MAX_CONN)
			sc->sc_numcon = UVISOR_MAX_CONN;

		/* Attach a ucom for each connection. */
		for (i = 0; i < sc->sc_numcon; ++i) {
			switch (coninfo.connections[i].port_function_id) {
			case UVISOR_FUNCTION_GENERIC:
				uca.info = "Generic";
				break;
			case UVISOR_FUNCTION_DEBUGGER:
				uca.info = "Debugger";
				break;
			case UVISOR_FUNCTION_HOTSYNC:
				uca.info = "HotSync";
				break;
			case UVISOR_FUNCTION_REMOTE_FILE_SYS:
				uca.info = "Remote File System";
				break;
			default:
				uca.info = "unknown";
				break;
			}
			port = coninfo.connections[i].port;
			uca.portno = port;
			uca.bulkin = port | UE_DIR_IN;
			uca.bulkout = port | UE_DIR_OUT;
			/* Verify that endpoints exist. */
			hasin = 0;
			hasout = 0;
			for (j = 0; j < id->bNumEndpoints; j++) {
				ed = usbd_interface2endpoint_descriptor(iface, j);
				if (ed == NULL)
					break;
				if (UE_GET_ADDR(ed->bEndpointAddress) == port &&
				    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
					if (UE_GET_DIR(ed->bEndpointAddress)
					    == UE_DIR_IN)
						hasin++;
					else
						hasout++;
				}
			}
			if (hasin == 1 && hasout == 1)
				sc->sc_subdevs[i] = config_found_sm(self, &uca,
				    ucomprint, ucomsubmatch);
			else
				printf("%s: no proper endpoints for port %d (%d,%d)\n",
				    sc->sc_dev.dv_xname, port, hasin, hasout);
		}
	} else {
		sc->sc_numcon = palmconinfo.num_ports;
		if (sc->sc_numcon > UVISOR_MAX_CONN)
			sc->sc_numcon = UVISOR_MAX_CONN;

		/* Attach a ucom for each connection. */
		for (i = 0; i < sc->sc_numcon; ++i) {
			/*
			 * XXX this should copy out 4-char string from the
			 * XXX port_function_id, but where would the string go?
			 * XXX uca.info is a const char *, not an array.
			 */
			uca.info = "sync";
			uca.portno = i;
			if (palmconinfo.endpoint_numbers_different) {
				port = palmconinfo.connections[i].end_point_info;
				uca.bulkin = (port >> 4) | UE_DIR_IN;
				uca.bulkout = (port & 0xf) | UE_DIR_OUT;
			} else {
				port = palmconinfo.connections[i].port;
				uca.bulkin = port | UE_DIR_IN;
				uca.bulkout = port | UE_DIR_OUT;
			}
			sc->sc_subdevs[i] = config_found_sm(self, &uca,
			    ucomprint, ucomsubmatch);
		}
	}

	return;

bad:
	DPRINTF(("uvisor_attach: ATTACH ERROR\n"));
	usbd_deactivate(sc->sc_udev);
}

int
uvisor_detach(struct device *self, int flags)
{
	struct uvisor_softc *sc = (struct uvisor_softc *)self;
	int rv = 0;
	int i;

	DPRINTF(("uvisor_detach: sc=%p flags=%d\n", sc, flags));
	for (i = 0; i < sc->sc_numcon; i++) {
		if (sc->sc_subdevs[i] != NULL) {
			rv |= config_detach(sc->sc_subdevs[i], flags);
			sc->sc_subdevs[i] = NULL;
		}
	}

	return (rv);
}

usbd_status
uvisor_init(struct uvisor_softc *sc, struct uvisor_connection_info *ci,
    struct uvisor_palm_connection_info *cpi)
{
	usbd_status err = 0;
	usb_device_request_t req;
	int actlen;
	uWord avail;

	if (sc->sc_flags & PALM4) {
		DPRINTF(("uvisor_init: getting Palm connection info\n"));
		req.bmRequestType = UT_READ_VENDOR_ENDPOINT;
		req.bRequest = UVISOR_GET_PALM_INFORMATION;
		USETW(req.wValue, 0);
		USETW(req.wIndex, 0);
		USETW(req.wLength, UVISOR_GET_PALM_INFORMATION_LEN);
		err = usbd_do_request_flags(sc->sc_udev, &req, cpi,
		    USBD_SHORT_XFER_OK, &actlen, USBD_DEFAULT_TIMEOUT);
		if (err == USBD_STALLED && sc->sc_vendor == USB_VENDOR_SONY) {
			/* some sony clie devices stall on palm4 requests,
			 * switch them over to using visor. dont do free space
			 * checks on them since they dont like them either.
			 */
			DPRINTF(("switching role for CLIE probe\n"));
			sc->sc_flags = CLIE4;
			err = 0;
		}
		if (err)
			return (err);
	}
	
	if (sc->sc_flags & VISOR) {
		DPRINTF(("uvisor_init: getting Visor connection info\n"));
		req.bmRequestType = UT_READ_VENDOR_ENDPOINT;
		req.bRequest = UVISOR_GET_CONNECTION_INFORMATION;
		USETW(req.wValue, 0);
		USETW(req.wIndex, 0);
		USETW(req.wLength, UVISOR_CONNECTION_INFO_SIZE);
		err = usbd_do_request_flags(sc->sc_udev, &req, ci,
		    USBD_SHORT_XFER_OK, &actlen, USBD_DEFAULT_TIMEOUT);
		if (err)
			return (err);
	}
	
	if (sc->sc_flags & NOFRE)
		return (err);
	
	DPRINTF(("uvisor_init: getting available bytes\n"));
	req.bmRequestType = UT_READ_VENDOR_ENDPOINT;
	req.bRequest = UVISOR_REQUEST_BYTES_AVAILABLE;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 5);
	USETW(req.wLength, sizeof avail);
	err = usbd_do_request(sc->sc_udev, &req, &avail);
	if (err)
		return (err);
	DPRINTF(("uvisor_init: avail=%d\n", UGETW(avail)));
	DPRINTF(("uvisor_init: done\n"));
	return (err);
}

void
uvisor_close(void *addr, int portno)
{
	struct uvisor_softc *sc = addr;
	usb_device_request_t req;
	struct uvisor_connection_info coninfo; /* XXX ? */
	int actlen;

	if (usbd_is_dying(sc->sc_udev))
		return;

	req.bmRequestType = UT_READ_VENDOR_ENDPOINT; /* XXX read? */
	req.bRequest = UVISOR_CLOSE_NOTIFICATION;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, UVISOR_CONNECTION_INFO_SIZE);
	(void)usbd_do_request_flags(sc->sc_udev, &req, &coninfo,
		  USBD_SHORT_XFER_OK, &actlen, USBD_DEFAULT_TIMEOUT);
}
