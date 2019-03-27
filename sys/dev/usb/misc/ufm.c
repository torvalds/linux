/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 M. Warner Losh
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This code is based on ugen.c and ulpt.c developed by Lennart Augustsson.
 * This code includes software developed by the NetBSD Foundation, Inc. and
 * its contributors.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


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
#include <sys/conf.h>
#include <sys/fcntl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR usb_debug
#include <dev/usb/usb_debug.h>

#include <dev/usb/ufm_ioctl.h>

#define	UFM_CMD0		0x00
#define	UFM_CMD_SET_FREQ	0x01
#define	UFM_CMD2		0x02

struct ufm_softc {
	struct usb_fifo_sc sc_fifo;
	struct mtx sc_mtx;

	struct usb_device *sc_udev;

	uint32_t sc_unit;
	uint32_t sc_freq;

	uint8_t	sc_name[16];
};

/* prototypes */

static device_probe_t ufm_probe;
static device_attach_t ufm_attach;
static device_detach_t ufm_detach;

static usb_fifo_ioctl_t ufm_ioctl;

static struct usb_fifo_methods ufm_fifo_methods = {
	.f_ioctl = &ufm_ioctl,
	.basename[0] = "ufm",
};

static int	ufm_do_req(struct ufm_softc *, uint8_t, uint16_t, uint16_t,
		    uint8_t *);
static int	ufm_set_freq(struct ufm_softc *, void *);
static int	ufm_get_freq(struct ufm_softc *, void *);
static int	ufm_start(struct ufm_softc *, void *);
static int	ufm_stop(struct ufm_softc *, void *);
static int	ufm_get_stat(struct ufm_softc *, void *);

static devclass_t ufm_devclass;

static device_method_t ufm_methods[] = {
	DEVMETHOD(device_probe, ufm_probe),
	DEVMETHOD(device_attach, ufm_attach),
	DEVMETHOD(device_detach, ufm_detach),

	DEVMETHOD_END
};

static driver_t ufm_driver = {
	.name = "ufm",
	.methods = ufm_methods,
	.size = sizeof(struct ufm_softc),
};

static const STRUCT_USB_HOST_ID ufm_devs[] = {
	{USB_VPI(USB_VENDOR_CYPRESS, USB_PRODUCT_CYPRESS_FMRADIO, 0)},
};

DRIVER_MODULE(ufm, uhub, ufm_driver, ufm_devclass, NULL, 0);
MODULE_DEPEND(ufm, usb, 1, 1, 1);
MODULE_VERSION(ufm, 1);
USB_PNP_HOST_INFO(ufm_devs);

static int
ufm_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != 0)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != 0)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(ufm_devs, sizeof(ufm_devs), uaa));
}

static int
ufm_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct ufm_softc *sc = device_get_softc(dev);
	int error;

	sc->sc_udev = uaa->device;
	sc->sc_unit = device_get_unit(dev);

	snprintf(sc->sc_name, sizeof(sc->sc_name), "%s",
	    device_get_nameunit(dev));

	mtx_init(&sc->sc_mtx, "ufm lock", NULL, MTX_DEF | MTX_RECURSE);

	device_set_usb_desc(dev);

	error = usb_fifo_attach(uaa->device, sc, &sc->sc_mtx,
	    &ufm_fifo_methods, &sc->sc_fifo,
	    device_get_unit(dev), -1, uaa->info.bIfaceIndex,
	    UID_ROOT, GID_OPERATOR, 0644);
	if (error) {
		goto detach;
	}
	return (0);			/* success */

detach:
	ufm_detach(dev);
	return (ENXIO);
}

static int
ufm_detach(device_t dev)
{
	struct ufm_softc *sc = device_get_softc(dev);

	usb_fifo_detach(&sc->sc_fifo);

	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static int
ufm_do_req(struct ufm_softc *sc, uint8_t request,
    uint16_t value, uint16_t index, uint8_t *retbuf)
{
	int error;

	struct usb_device_request req;
	uint8_t buf[1];

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = request;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, 1);

	error = usbd_do_request(sc->sc_udev, NULL, &req, buf);

	if (retbuf) {
		*retbuf = buf[0];
	}
	if (error) {
		return (ENXIO);
	}
	return (0);
}

static int
ufm_set_freq(struct ufm_softc *sc, void *addr)
{
	int freq = *(int *)addr;

	/*
	 * Freq now is in Hz.  We need to convert it to the frequency
	 * that the radio wants.  This frequency is 10.7MHz above
	 * the actual frequency.  We then need to convert to
	 * units of 12.5kHz.  We add one to the IFM to make rounding
	 * easier.
	 */
	mtx_lock(&sc->sc_mtx);
	sc->sc_freq = freq;
	mtx_unlock(&sc->sc_mtx);

	freq = (freq + 10700001) / 12500;

	/* This appears to set the frequency */
	if (ufm_do_req(sc, UFM_CMD_SET_FREQ,
	    freq >> 8, freq, NULL) != 0) {
		return (EIO);
	}
	/* Not sure what this does */
	if (ufm_do_req(sc, UFM_CMD0,
	    0x96, 0xb7, NULL) != 0) {
		return (EIO);
	}
	return (0);
}

static int
ufm_get_freq(struct ufm_softc *sc, void *addr)
{
	int *valp = (int *)addr;

	mtx_lock(&sc->sc_mtx);
	*valp = sc->sc_freq;
	mtx_unlock(&sc->sc_mtx);
	return (0);
}

static int
ufm_start(struct ufm_softc *sc, void *addr)
{
	uint8_t ret;

	if (ufm_do_req(sc, UFM_CMD0,
	    0x00, 0xc7, &ret)) {
		return (EIO);
	}
	if (ufm_do_req(sc, UFM_CMD2,
	    0x01, 0x00, &ret)) {
		return (EIO);
	}
	if (ret & 0x1) {
		return (EIO);
	}
	return (0);
}

static int
ufm_stop(struct ufm_softc *sc, void *addr)
{
	if (ufm_do_req(sc, UFM_CMD0,
	    0x16, 0x1C, NULL)) {
		return (EIO);
	}
	if (ufm_do_req(sc, UFM_CMD2,
	    0x00, 0x00, NULL)) {
		return (EIO);
	}
	return (0);
}

static int
ufm_get_stat(struct ufm_softc *sc, void *addr)
{
	uint8_t ret;

	/*
	 * Note, there's a 240ms settle time before the status
	 * will be valid, so sleep that amount.
	 */
	usb_pause_mtx(NULL, hz / 4);

	if (ufm_do_req(sc, UFM_CMD0,
	    0x00, 0x24, &ret)) {
		return (EIO);
	}
	*(int *)addr = ret;

	return (0);
}

static int
ufm_ioctl(struct usb_fifo *fifo, u_long cmd, void *addr,
    int fflags)
{
	struct ufm_softc *sc = usb_fifo_softc(fifo);
	int error = 0;

	if ((fflags & (FWRITE | FREAD)) != (FWRITE | FREAD)) {
		return (EACCES);
	}

	switch (cmd) {
	case FM_SET_FREQ:
		error = ufm_set_freq(sc, addr);
		break;
	case FM_GET_FREQ:
		error = ufm_get_freq(sc, addr);
		break;
	case FM_START:
		error = ufm_start(sc, addr);
		break;
	case FM_STOP:
		error = ufm_stop(sc, addr);
		break;
	case FM_GET_STAT:
		error = ufm_get_stat(sc, addr);
		break;
	default:
		error = ENOTTY;
		break;
	}
	return (error);
}
