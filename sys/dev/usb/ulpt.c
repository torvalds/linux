/*	$OpenBSD: ulpt.c,v 1.59 2024/05/13 01:15:53 jsg Exp $ */
/*	$NetBSD: ulpt.c,v 1.57 2003/01/05 10:19:42 scw Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/ulpt.c,v 1.24 1999/11/17 22:33:44 n_hibma Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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
 * Printer Class spec:
 *   https://www.usb.org/sites/default/files/usbprint11a021811.pdf
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/vnode.h>
#include <sys/syslog.h>
#include <sys/malloc.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/usb_quirks.h>

#define	LPTPRI		(PZERO+8)
#define	ULPT_BSIZE	16384

#ifdef ULPT_DEBUG
#define DPRINTF(x)	do { if (ulptdebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (ulptdebug>(n)) printf x; } while (0)
int	ulptdebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#define UR_GET_DEVICE_ID 0
#define UR_GET_PORT_STATUS 1
#define UR_SOFT_RESET 2

#define	LPS_NERR		0x08	/* printer no error */
#define	LPS_SELECT		0x10	/* printer selected */
#define	LPS_NOPAPER		0x20	/* printer out of paper */
#define LPS_INVERT      (LPS_SELECT|LPS_NERR)
#define LPS_MASK        (LPS_SELECT|LPS_NERR|LPS_NOPAPER)

struct ulpt_softc {
	struct device sc_dev;
	struct usbd_device *sc_udev;	/* device */
	struct usbd_interface *sc_iface;/* interface */
	int sc_ifaceno;

	int sc_out;
	struct usbd_pipe *sc_out_pipe;	/* bulk out pipe */

	int sc_in;
	struct usbd_pipe *sc_in_pipe;	/* bulk in pipe */
	struct usbd_xfer *sc_in_xfer1;
	struct usbd_xfer *sc_in_xfer2;
	u_char sc_junk[64];	/* somewhere to dump input */

	u_char sc_state;
#define	ULPT_OPEN	0x01	/* device is open */
#define	ULPT_OBUSY	0x02	/* printer is busy doing output */
#define	ULPT_INIT	0x04	/* waiting to initialize for open */
	u_char sc_flags;
#define	ULPT_NOPRIME	0x40	/* don't prime on open */
#define	ULPT_EFIRMWARE	0x80	/* error loading firmware */
	u_char sc_laststatus;

	int sc_refcnt;

	struct ulpt_fwdev *sc_fwdev;
};

int ulpt_do_write(struct ulpt_softc *, struct uio *uio, int);
int ulpt_status(struct ulpt_softc *);
void ulpt_reset(struct ulpt_softc *);
int ulpt_statusmsg(u_char, struct ulpt_softc *);

/*
 * Printers which need firmware uploads.
 */
void ulpt_load_firmware(struct device *);
usbd_status ulpt_ucode_loader_hp(struct ulpt_softc *);
struct ulpt_fwdev {
	struct usb_devno	 uv_dev;
	char			*ucode_name;
	usbd_status		 (*ucode_loader)(struct ulpt_softc *);
} ulpt_fwdevs[] = {
	{
	    { USB_VENDOR_HP, USB_PRODUCT_HP_1000 },
	    "ulpt-hp1000",
	    ulpt_ucode_loader_hp
	},
	{
	    { USB_VENDOR_HP, USB_PRODUCT_HP_1005 },
	    "ulpt-hp1005",
	    ulpt_ucode_loader_hp
	},
	{
	    { USB_VENDOR_HP, USB_PRODUCT_HP_1018 },
	    "ulpt-hp1018",
	    ulpt_ucode_loader_hp
	},
	{
	    { USB_VENDOR_HP, USB_PRODUCT_HP_1020 },
	    "ulpt-hp1020",
	    ulpt_ucode_loader_hp
	},
};

#if 0
void ieee1284_print_id(char *);
#endif

#define	ULPTUNIT(s)	(minor(s) & 0x1f)
#define	ULPTFLAGS(s)	(minor(s) & 0xe0)


int ulpt_match(struct device *, void *, void *);
void ulpt_attach(struct device *, struct device *, void *);
int ulpt_detach(struct device *, int);

struct cfdriver ulpt_cd = {
	NULL, "ulpt", DV_DULL
};

const struct cfattach ulpt_ca = {
	sizeof(struct ulpt_softc), ulpt_match, ulpt_attach, ulpt_detach
};

int
ulpt_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;

	DPRINTFN(10,("ulpt_match\n"));
	if (uaa->iface == NULL)
		return (UMATCH_NONE);
	id = usbd_get_interface_descriptor(uaa->iface);
	if (id != NULL &&
	    id->bInterfaceClass == UICLASS_PRINTER &&
	    id->bInterfaceSubClass == UISUBCLASS_PRINTER &&
	    ((id->bInterfaceProtocol == UIPROTO_PRINTER_UNI) ||
	     (id->bInterfaceProtocol == UIPROTO_PRINTER_BI) ||
	     (id->bInterfaceProtocol == UIPROTO_PRINTER_1284)))
		return (UMATCH_IFACECLASS_IFACESUBCLASS_IFACEPROTO);
	return (UMATCH_NONE);
}

void
ulpt_load_firmware(struct device *self)
{
	struct ulpt_softc *sc = (struct ulpt_softc *)self;
	usbd_status err;

	err = (sc->sc_fwdev->ucode_loader)(sc);
	if (err != USBD_NORMAL_COMPLETION) {
		sc->sc_flags |= ULPT_EFIRMWARE;
		printf("%s: could not load firmware '%s'\n",
		    sc->sc_dev.dv_xname, sc->sc_fwdev->ucode_name);
	} else
		sc->sc_flags &= ~ULPT_EFIRMWARE;
}

#define ulpt_lookup(v, p) \
	((struct ulpt_fwdev *)usb_lookup(ulpt_fwdevs, v, p))

void
ulpt_attach(struct device *parent, struct device *self, void *aux)
{
	struct ulpt_softc *sc = (struct ulpt_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_device *dev = uaa->device;
	struct usbd_interface *iface = uaa->iface;
	usb_interface_descriptor_t *ifcd = usbd_get_interface_descriptor(iface);
	usb_interface_descriptor_t *id, *iend;
	usb_config_descriptor_t *cdesc;
	usbd_status err;
	usb_endpoint_descriptor_t *ed;
	int i, altno;

	DPRINTFN(10,("ulpt_attach: sc=%p\n", sc));

	//printf("%s: iclass %d/%d\n", sc->sc_dev.dv_xname,
	//    ifcd->bInterfaceClass, ifcd->bInterfaceSubClass);

	/* XXX
	 * Stepping through the alternate settings needs to be abstracted out.
	 */
	cdesc = usbd_get_config_descriptor(dev);
	if (cdesc == NULL) {
		printf("%s: failed to get configuration descriptor\n",
		       sc->sc_dev.dv_xname);
		return;
	}
	iend = (usb_interface_descriptor_t *)
		   ((char *)cdesc + UGETW(cdesc->wTotalLength));
#ifdef DIAGNOSTIC
	if (ifcd < (usb_interface_descriptor_t *)cdesc ||
	    ifcd >= iend)
		panic("ulpt: iface desc out of range");
#endif
	/* Step through all the descriptors looking for bidir mode */
	for (id = ifcd, altno = 0;
	     id < iend;
	     id = (void *)((char *)id + id->bLength)) {
		if (id->bDescriptorType == UDESC_INTERFACE &&
		    id->bInterfaceNumber == ifcd->bInterfaceNumber) {
			if (id->bInterfaceClass == UICLASS_PRINTER &&
			    id->bInterfaceSubClass == UISUBCLASS_PRINTER &&
			    (id->bInterfaceProtocol == UIPROTO_PRINTER_BI /*||
			     id->bInterfaceProtocol == UIPROTO_PRINTER_1284*/))
				goto found;
			altno++;
		}
	}
	id = ifcd;		/* not found, use original */
 found:
	if (id != ifcd) {
		/* Found a new bidir setting */
		DPRINTF(("ulpt_attach: set altno = %d\n", altno));
		err = usbd_set_interface(iface, altno);
		if (err) {
			printf("%s: setting alternate interface failed\n",
			       sc->sc_dev.dv_xname);
			usbd_deactivate(sc->sc_udev);
			return;
		}
	}


	sc->sc_in = -1;
	sc->sc_out = -1;

	id = usbd_get_interface_descriptor(iface);
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(iface, i);
		if (ed == NULL) {
			printf("%s: couldn't get ep %d\n",
			    sc->sc_dev.dv_xname, i);
			return;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->sc_in = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->sc_out = ed->bEndpointAddress;
		}
	}
	if (sc->sc_out == -1) {
		printf("%s: could not find bulk out endpoint\n",
		    sc->sc_dev.dv_xname);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	if (usbd_get_quirks(dev)->uq_flags & UQ_BROKEN_BIDIR) {
		/* This device doesn't handle reading properly. */
		sc->sc_in = -1;
	}

	printf("%s: using %s-directional mode\n", sc->sc_dev.dv_xname,
	       sc->sc_in >= 0 ? "bi" : "uni");

	DPRINTFN(10, ("ulpt_attach: bulk=%d\n", sc->sc_out));

	sc->sc_iface = iface;
	sc->sc_ifaceno = id->bInterfaceNumber;
	sc->sc_udev = dev;

	/* maybe the device needs firmware */
	sc->sc_fwdev = ulpt_lookup(uaa->vendor, uaa->product);
	if (sc->sc_fwdev)
		config_mountroot(self, ulpt_load_firmware);

#if 0
/*
 * This code is disabled because for some mysterious reason it causes
 * printing not to work.  But only sometimes, and mostly with
 * UHCI and less often with OHCI.  *sigh*
 */
	{
	usb_config_descriptor_t *cd = usbd_get_config_descriptor(dev);
	usb_device_request_t req;
	int len, alen;

	req.bmRequestType = UT_READ_CLASS_INTERFACE;
	req.bRequest = UR_GET_DEVICE_ID;
	USETW(req.wValue, cd->bConfigurationValue);
	USETW2(req.wIndex, id->bInterfaceNumber, id->bAlternateSetting);
	USETW(req.wLength, DEVINFOSIZE - 1);
	err = usbd_do_request_flags(dev, &req, devinfop, USBD_SHORT_XFER_OK,
		  &alen, USBD_DEFAULT_TIMEOUT);
	if (err) {
		printf("%s: cannot get device id\n", sc->sc_dev.dv_xname);
	} else if (alen <= 2) {
		printf("%s: empty device id, no printer connected?\n",
		       sc->sc_dev.dv_xname);
	} else {
		/* devinfop now contains an IEEE-1284 device ID */
		len = ((devinfop[0] & 0xff) << 8) | (devinfop[1] & 0xff);
		if (len > DEVINFOSIZE - 3)
			len = DEVINFOSIZE - 3;
		devinfo[len] = 0;
		printf("%s: device id <", sc->sc_dev.dv_xname);
		ieee1284_print_id(devinfop+2);
		printf(">\n");
	}
	}
#endif
}

int
ulpt_detach(struct device *self, int flags)
{
	struct ulpt_softc *sc = (struct ulpt_softc *)self;
	int s;
	int maj, mn;

	DPRINTF(("ulpt_detach: sc=%p\n", sc));

	if (sc->sc_out_pipe != NULL)
		usbd_abort_pipe(sc->sc_out_pipe);
	if (sc->sc_in_pipe != NULL)
		usbd_abort_pipe(sc->sc_in_pipe);

	s = splusb();
	if (--sc->sc_refcnt >= 0) {
		/* There is noone to wake, aborting the pipe is enough */
		/* Wait for processes to go away. */
		usb_detach_wait(&sc->sc_dev);
	}
	splx(s);

	/* locate the major number */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == ulptopen)
			break;

	/* Nuke the vnodes for any open instances (calls close). */
	mn = self->dv_unit;
	vdevgone(maj, mn, mn, VCHR);
	vdevgone(maj, mn | ULPT_NOPRIME , mn | ULPT_NOPRIME, VCHR);

	return (0);
}

int
ulpt_status(struct ulpt_softc *sc)
{
	usb_device_request_t req;
	usbd_status err;
	u_char status;

	req.bmRequestType = UT_READ_CLASS_INTERFACE;
	req.bRequest = UR_GET_PORT_STATUS;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_ifaceno);
	USETW(req.wLength, 1);
	err = usbd_do_request(sc->sc_udev, &req, &status);
	DPRINTFN(1, ("ulpt_status: status=0x%02x err=%d\n", status, err));
	if (!err)
		return (status);
	else
		return (0);
}

void
ulpt_reset(struct ulpt_softc *sc)
{
	usb_device_request_t req;

	DPRINTFN(1, ("ulpt_reset\n"));
	req.bRequest = UR_SOFT_RESET;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_ifaceno);
	USETW(req.wLength, 0);

	/*
	 * There was a mistake in the USB printer 1.0 spec that gave the
	 * request type as UT_WRITE_CLASS_OTHER; it should have been
	 * UT_WRITE_CLASS_INTERFACE.  Many printers use the old one,
	 * so we try both.
	 */
	req.bmRequestType = UT_WRITE_CLASS_OTHER;
	if (usbd_do_request(sc->sc_udev, &req, 0)) {	/* 1.0 */
		req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
		(void)usbd_do_request(sc->sc_udev, &req, 0); /* 1.1 */
	}
}

static void
ulpt_input(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct ulpt_softc *sc = priv;

	DPRINTFN(2,("ulpt_input: got some data\n"));
	/* Do it again. */
	if (xfer == sc->sc_in_xfer1)
		usbd_transfer(sc->sc_in_xfer2);
	else
		usbd_transfer(sc->sc_in_xfer1);
}

int ulptusein = 1;

/*
 * Reset the printer, then wait until it's selected and not busy.
 */
int
ulptopen(dev_t dev, int flag, int mode, struct proc *p)
{
	u_char flags = ULPTFLAGS(dev);
	struct ulpt_softc *sc;
	usbd_status err;
	int error;

	if (ULPTUNIT(dev) >= ulpt_cd.cd_ndevs)
		return (ENXIO);
	sc = ulpt_cd.cd_devs[ULPTUNIT(dev)];
	if (sc == NULL)
		return (ENXIO);

	if (sc == NULL || sc->sc_iface == NULL || usbd_is_dying(sc->sc_udev))
		return (ENXIO);

	if (sc->sc_state)
		return (EBUSY);

	/* If a previous attempt to load firmware failed, retry. */
	if (sc->sc_flags & ULPT_EFIRMWARE) {
		ulpt_load_firmware(&sc->sc_dev);
		if (sc->sc_flags & ULPT_EFIRMWARE)
			return (EIO);
	}

	sc->sc_state = ULPT_INIT;
	sc->sc_flags = flags;
	DPRINTF(("ulptopen: flags=0x%x\n", (unsigned)flags));

	error = 0;
	sc->sc_refcnt++;

	if ((flags & ULPT_NOPRIME) == 0) {
		ulpt_reset(sc);
		if (usbd_is_dying(sc->sc_udev)) {
			error = ENXIO;
			sc->sc_state = 0;
			goto done;
		}
	}

	err = usbd_open_pipe(sc->sc_iface, sc->sc_out, 0, &sc->sc_out_pipe);
	if (err) {
		sc->sc_state = 0;
		error = EIO;
		goto done;
	}
	if (ulptusein && sc->sc_in != -1) {
		DPRINTF(("ulpt_open: open input pipe\n"));
		err = usbd_open_pipe(sc->sc_iface, sc->sc_in,0,&sc->sc_in_pipe);
		if (err) {
			error = EIO;
			usbd_close_pipe(sc->sc_out_pipe);
			sc->sc_out_pipe = NULL;
			sc->sc_state = 0;
			goto done;
		}
		sc->sc_in_xfer1 = usbd_alloc_xfer(sc->sc_udev);
		sc->sc_in_xfer2 = usbd_alloc_xfer(sc->sc_udev);
		if (sc->sc_in_xfer1 == NULL || sc->sc_in_xfer2 == NULL) {
			error = ENOMEM;
			if (sc->sc_in_xfer1 != NULL) {
				usbd_free_xfer(sc->sc_in_xfer1);
				sc->sc_in_xfer1 = NULL;
			}
			if (sc->sc_in_xfer2 != NULL) {
				usbd_free_xfer(sc->sc_in_xfer2);
				sc->sc_in_xfer2 = NULL;
			}
			usbd_close_pipe(sc->sc_out_pipe);
			sc->sc_out_pipe = NULL;
			usbd_close_pipe(sc->sc_in_pipe);
			sc->sc_in_pipe = NULL;
			sc->sc_state = 0;
			goto done;
		}
		usbd_setup_xfer(sc->sc_in_xfer1, sc->sc_in_pipe, sc,
		    sc->sc_junk, sizeof sc->sc_junk, USBD_SHORT_XFER_OK,
		    USBD_NO_TIMEOUT, ulpt_input);
		usbd_setup_xfer(sc->sc_in_xfer2, sc->sc_in_pipe, sc,
		    sc->sc_junk, sizeof sc->sc_junk, USBD_SHORT_XFER_OK,
		    USBD_NO_TIMEOUT, ulpt_input);
		usbd_transfer(sc->sc_in_xfer1); /* ignore failed start */
	}

	sc->sc_state = ULPT_OPEN;

 done:
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);

	DPRINTF(("ulptopen: done, error=%d\n", error));
	return (error);
}

int
ulpt_statusmsg(u_char status, struct ulpt_softc *sc)
{
	u_char new;

	status = (status ^ LPS_INVERT) & LPS_MASK;
	new = status & ~sc->sc_laststatus;
	sc->sc_laststatus = status;

	if (new & LPS_SELECT)
		log(LOG_NOTICE, "%s: offline\n", sc->sc_dev.dv_xname);
	else if (new & LPS_NOPAPER)
		log(LOG_NOTICE, "%s: out of paper\n", sc->sc_dev.dv_xname);
	else if (new & LPS_NERR)
		log(LOG_NOTICE, "%s: output error\n", sc->sc_dev.dv_xname);

	return (status);
}

int
ulptclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct ulpt_softc *sc;

	sc = ulpt_cd.cd_devs[ULPTUNIT(dev)];

	if (sc->sc_state != ULPT_OPEN)
		/* We are being forced to close before the open completed. */
		return (0);

	if (sc->sc_out_pipe != NULL) {
		usbd_close_pipe(sc->sc_out_pipe);
		sc->sc_out_pipe = NULL;
	}
	if (sc->sc_in_pipe != NULL) {
		usbd_close_pipe(sc->sc_in_pipe);
		sc->sc_in_pipe = NULL;
		if (sc->sc_in_xfer1 != NULL) {
			usbd_free_xfer(sc->sc_in_xfer1);
			sc->sc_in_xfer1 = NULL;
		}
		if (sc->sc_in_xfer2 != NULL) {
			usbd_free_xfer(sc->sc_in_xfer2);
			sc->sc_in_xfer2 = NULL;
		}
	}

	sc->sc_state = 0;

	DPRINTF(("ulptclose: closed\n"));
	return (0);
}

int
ulpt_do_write(struct ulpt_softc *sc, struct uio *uio, int flags)
{
	size_t n;
	int error = 0;
	void *bufp;
	struct usbd_xfer *xfer;
	usbd_status err;

	DPRINTF(("ulptwrite\n"));
	xfer = usbd_alloc_xfer(sc->sc_udev);
	if (xfer == NULL)
		return (ENOMEM);
	bufp = usbd_alloc_buffer(xfer, ULPT_BSIZE);
	if (bufp == NULL) {
		usbd_free_xfer(xfer);
		return (ENOMEM);
	}
	while ((n = ulmin(ULPT_BSIZE, uio->uio_resid)) != 0) {
		ulpt_statusmsg(ulpt_status(sc), sc);
		error = uiomove(bufp, n, uio);
		if (error)
			break;
		DPRINTFN(1, ("ulptwrite: transfer %zu bytes\n", n));
		usbd_setup_xfer(xfer, sc->sc_out_pipe, 0, bufp, n,
		    USBD_NO_COPY | USBD_SYNCHRONOUS | USBD_CATCH, 0, NULL);
		err = usbd_transfer(xfer);
		if (err) {
			usbd_clear_endpoint_stall(sc->sc_out_pipe);
			DPRINTF(("ulptwrite: error=%d\n", err));
			error = EIO;
			break;
		}
	}
	usbd_free_xfer(xfer);

	return (error);
}

int
ulptwrite(dev_t dev, struct uio *uio, int flags)
{
	struct ulpt_softc *sc;
	int error;

	sc = ulpt_cd.cd_devs[ULPTUNIT(dev)];

	if (usbd_is_dying(sc->sc_udev) || (sc->sc_flags & ULPT_EFIRMWARE))
		return (EIO);

	sc->sc_refcnt++;
	error = ulpt_do_write(sc, uio, flags);
	if (--sc->sc_refcnt < 0)
		usb_detach_wakeup(&sc->sc_dev);
	return (error);
}

usbd_status
ulpt_ucode_loader_hp(struct ulpt_softc *sc)
{
	usbd_status error;
	int load_error;
	uint8_t *ucode;
	uint32_t len;
	size_t ucode_size;
	const char *ucode_name = sc->sc_fwdev->ucode_name;
	int offset = 0, remain;
	struct usbd_xfer *xfer;
	void *bufp;

	/* open microcode file */
	load_error = loadfirmware(ucode_name, &ucode, &ucode_size);
	if (load_error != 0) {
		printf("%s: failed loadfirmware of file %s (error %d)\n",
		    sc->sc_dev.dv_xname, ucode_name, load_error);
		return (USBD_INVAL);
	}

	/* upload microcode */
	error = usbd_open_pipe(sc->sc_iface, sc->sc_out, 0, &sc->sc_out_pipe);
	if (error)
		goto free_ucode;
	xfer = usbd_alloc_xfer(sc->sc_udev);
	if (xfer == NULL)
		goto close_pipe;
	bufp = usbd_alloc_buffer(xfer, ULPT_BSIZE);
	if (bufp == NULL) {
		error = USBD_NOMEM;
		goto free_xfer;
	}
	remain = ucode_size;
	while (remain > 0) {
		len = min(remain, ULPT_BSIZE);
		memcpy(bufp, &ucode[offset], len);
		usbd_setup_xfer(xfer, sc->sc_out_pipe, 0, bufp, len,
		    USBD_NO_COPY | USBD_SYNCHRONOUS, 0, NULL);
		error = usbd_transfer(xfer);
		if (error != USBD_NORMAL_COMPLETION) {
			usbd_clear_endpoint_stall(sc->sc_out_pipe);
			printf("%s: ucode upload error=%s!\n",
			    sc->sc_dev.dv_xname, usbd_errstr(error));
			break;
		}
		DPRINTF(("%s: uploaded %d bytes ucode\n",
		    sc->sc_dev.dv_xname, len));

		offset += len;
		remain -= len;
	}
free_xfer:
	usbd_free_xfer(xfer);
close_pipe:
	usbd_close_pipe(sc->sc_out_pipe);
	sc->sc_out_pipe = NULL;
free_ucode:
	free(ucode, M_DEVBUF, ucode_size);

	return (error);
}

#if 0
/* XXX This does not belong here. */
/*
 * Print select parts of a IEEE 1284 device ID.
 */
void
ieee1284_print_id(char *str)
{
	char *p, *q;

	for (p = str-1; p; p = strchr(p, ';')) {
		p++;		/* skip ';' */
		if (strncmp(p, "MFG:", 4) == 0 ||
		    strncmp(p, "MANUFACTURER:", 14) == 0 ||
		    strncmp(p, "MDL:", 4) == 0 ||
		    strncmp(p, "MODEL:", 6) == 0) {
			q = strchr(p, ';');
			if (q)
				printf("%.*s", (int)(q - p + 1), p);
		}
	}
}
#endif
