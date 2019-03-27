/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Hans Petter Selasky. All rights reserved.
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
 * HID spec: http://www.usb.org/developers/devclass_docs/HID1_11.pdf
 */

#include <sys/param.h>
__FBSDID("$FreeBSD$");

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/linker_set.h>
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

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbhid.h>
#include "usb_if.h"

#define	USB_DEBUG_VAR g_mouse_debug
#include <dev/usb/usb_debug.h>

#include <dev/usb/gadget/g_mouse.h>

static SYSCTL_NODE(_hw_usb, OID_AUTO, g_mouse, CTLFLAG_RW, 0, "USB mouse gadget");

#ifdef USB_DEBUG
static int g_mouse_debug = 0;

SYSCTL_INT(_hw_usb_g_mouse, OID_AUTO, debug, CTLFLAG_RWTUN,
    &g_mouse_debug, 0, "Debug level");
#endif

static int g_mouse_mode = 0;

SYSCTL_INT(_hw_usb_g_mouse, OID_AUTO, mode, CTLFLAG_RWTUN,
    &g_mouse_mode, 0, "Mode selection");

static int g_mouse_button_press_interval = 0;

SYSCTL_INT(_hw_usb_g_mouse, OID_AUTO, button_press_interval, CTLFLAG_RWTUN,
    &g_mouse_button_press_interval, 0, "Mouse button update interval in milliseconds");

static int g_mouse_cursor_update_interval = 1023;

SYSCTL_INT(_hw_usb_g_mouse, OID_AUTO, cursor_update_interval, CTLFLAG_RWTUN,
    &g_mouse_cursor_update_interval, 0, "Mouse cursor update interval in milliseconds");

static int g_mouse_cursor_radius = 128;

SYSCTL_INT(_hw_usb_g_mouse, OID_AUTO, cursor_radius, CTLFLAG_RWTUN,
    &g_mouse_cursor_radius, 0, "Mouse cursor radius in pixels");

struct g_mouse_data {
	uint8_t buttons;
#define	BUT_0 0x01
#define	BUT_1 0x02
#define	BUT_2 0x04
	int8_t dx;
	int8_t dy;
	int8_t dz;
};

enum {
	G_MOUSE_INTR_DT,
	G_MOUSE_N_TRANSFER,
};

struct g_mouse_softc {
	struct mtx sc_mtx;
	struct usb_callout sc_button_press_callout;
	struct usb_callout sc_cursor_update_callout;
	struct g_mouse_data sc_data;
	struct usb_xfer *sc_xfer[G_MOUSE_N_TRANSFER];

	int	sc_mode;
	int	sc_radius;
	int	sc_last_x_state;
	int	sc_last_y_state;
	int	sc_curr_x_state;
	int	sc_curr_y_state;
	int	sc_tick;

	uint8_t sc_do_cursor_update;
	uint8_t sc_do_button_update;
};

static device_probe_t g_mouse_probe;
static device_attach_t g_mouse_attach;
static device_detach_t g_mouse_detach;
static usb_handle_request_t g_mouse_handle_request;
static usb_callback_t g_mouse_intr_callback;

static devclass_t g_mouse_devclass;

static device_method_t g_mouse_methods[] = {
	/* USB interface */
	DEVMETHOD(usb_handle_request, g_mouse_handle_request),

	/* Device interface */
	DEVMETHOD(device_probe, g_mouse_probe),
	DEVMETHOD(device_attach, g_mouse_attach),
	DEVMETHOD(device_detach, g_mouse_detach),

	DEVMETHOD_END
};

static driver_t g_mouse_driver = {
	.name = "g_mouse",
	.methods = g_mouse_methods,
	.size = sizeof(struct g_mouse_softc),
};

DRIVER_MODULE(g_mouse, uhub, g_mouse_driver, g_mouse_devclass, 0, 0);
MODULE_DEPEND(g_mouse, usb, 1, 1, 1);

static const struct usb_config g_mouse_config[G_MOUSE_N_TRANSFER] = {

	[G_MOUSE_INTR_DT] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.ext_buffer = 1,.pipe_bof = 1,},
		.bufsize = sizeof(struct g_mouse_data),
		.callback = &g_mouse_intr_callback,
		.frames = 1,
		.usb_mode = USB_MODE_DEVICE,
	},
};

static void g_mouse_button_press_timeout(void *arg);
static void g_mouse_cursor_update_timeout(void *arg);

static void
g_mouse_button_press_timeout_reset(struct g_mouse_softc *sc)
{
	int i = g_mouse_button_press_interval;

	if (i <= 0) {
		sc->sc_data.buttons = 0;
		sc->sc_do_button_update = 0;
	} else {
		sc->sc_do_button_update = 1;
	}

	if ((i <= 0) || (i > 1023))
		i = 1023;

	i = USB_MS_TO_TICKS(i);

	usb_callout_reset(&sc->sc_button_press_callout, i, 
	    &g_mouse_button_press_timeout, sc);
}

static void
g_mouse_cursor_update_timeout_reset(struct g_mouse_softc *sc)
{
	int i = g_mouse_cursor_update_interval;

	if (i <= 0) {
		sc->sc_data.dx = 0;
		sc->sc_data.dy = 0;
		sc->sc_do_cursor_update = 0;
		sc->sc_tick = 0;
	} else {
		sc->sc_do_cursor_update = 1;
	}

	if ((i <= 0) || (i > 1023))
		i = 1023;

	i = USB_MS_TO_TICKS(i);

	usb_callout_reset(&sc->sc_cursor_update_callout, i, 
	    &g_mouse_cursor_update_timeout, sc);
}

static void
g_mouse_update_mode_radius(struct g_mouse_softc *sc)
{
	sc->sc_mode = g_mouse_mode;
	sc->sc_radius = g_mouse_cursor_radius;

	if (sc->sc_radius < 0)
		sc->sc_radius = 0;
	else if (sc->sc_radius > 1023)
		sc->sc_radius = 1023;
}

static void
g_mouse_button_press_timeout(void *arg)
{
	struct g_mouse_softc *sc = arg;

	g_mouse_update_mode_radius(sc);

	DPRINTFN(11, "Timeout %p (button press)\n", sc->sc_xfer[G_MOUSE_INTR_DT]);

	g_mouse_button_press_timeout_reset(sc);

	usbd_transfer_start(sc->sc_xfer[G_MOUSE_INTR_DT]);
}

static void
g_mouse_cursor_update_timeout(void *arg)
{
	struct g_mouse_softc *sc = arg;

	g_mouse_update_mode_radius(sc);

	DPRINTFN(11, "Timeout %p (cursor update)\n", sc->sc_xfer[G_MOUSE_INTR_DT]);

	g_mouse_cursor_update_timeout_reset(sc);

	usbd_transfer_start(sc->sc_xfer[G_MOUSE_INTR_DT]);
}

static int
g_mouse_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	DPRINTFN(11, "\n");

	if (uaa->usb_mode != USB_MODE_DEVICE)
		return (ENXIO);

	if ((uaa->info.bInterfaceClass == UICLASS_HID) &&
	    (uaa->info.bInterfaceSubClass == UISUBCLASS_BOOT) &&
	    (uaa->info.bInterfaceProtocol == UIPROTO_MOUSE))
		return (0);

	return (ENXIO);
}

static int
g_mouse_attach(device_t dev)
{
	struct g_mouse_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	int error;

	DPRINTFN(11, "\n");

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, "g_mouse", NULL, MTX_DEF);

	usb_callout_init_mtx(&sc->sc_button_press_callout, &sc->sc_mtx, 0);
	usb_callout_init_mtx(&sc->sc_cursor_update_callout, &sc->sc_mtx, 0);

	sc->sc_mode = G_MOUSE_MODE_SILENT;

	error = usbd_transfer_setup(uaa->device,
	    &uaa->info.bIfaceIndex, sc->sc_xfer, g_mouse_config,
	    G_MOUSE_N_TRANSFER, sc, &sc->sc_mtx);

	if (error) {
		DPRINTF("error=%s\n", usbd_errstr(error));
		goto detach;
	}

	mtx_lock(&sc->sc_mtx);
	g_mouse_button_press_timeout_reset(sc);
	g_mouse_cursor_update_timeout_reset(sc);
	mtx_unlock(&sc->sc_mtx);

	return (0);			/* success */

detach:
	g_mouse_detach(dev);

	return (ENXIO);			/* error */
}

static int
g_mouse_detach(device_t dev)
{
	struct g_mouse_softc *sc = device_get_softc(dev);

	DPRINTF("\n");

	mtx_lock(&sc->sc_mtx);
	usb_callout_stop(&sc->sc_button_press_callout);
	usb_callout_stop(&sc->sc_cursor_update_callout);
	mtx_unlock(&sc->sc_mtx);

	usbd_transfer_unsetup(sc->sc_xfer, G_MOUSE_N_TRANSFER);

	usb_callout_drain(&sc->sc_button_press_callout);
	usb_callout_drain(&sc->sc_cursor_update_callout);

	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static void
g_mouse_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct g_mouse_softc *sc = usbd_xfer_softc(xfer);
	int actlen;
	int aframes;
	int dx;
	int dy;
	int radius;

	usbd_xfer_status(xfer, &actlen, NULL, &aframes, NULL);

	DPRINTF("st=%d aframes=%d actlen=%d bytes\n",
	    USB_GET_STATE(xfer), aframes, actlen);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		if (!(sc->sc_do_cursor_update || sc->sc_do_button_update))
			break;

	case USB_ST_SETUP:
tr_setup:

	  if (sc->sc_do_cursor_update) {
		sc->sc_do_cursor_update = 0;
		sc->sc_tick += 80;
		if ((sc->sc_tick < 0) || (sc->sc_tick > 7999))
			sc->sc_tick = 0;
	  }

	  if (sc->sc_do_button_update) {
			sc->sc_do_button_update = 0;
			sc->sc_data.buttons ^= BUT_0;
	  }

	  radius = sc->sc_radius;

		switch (sc->sc_mode) {
		case G_MOUSE_MODE_SILENT:
			sc->sc_data.buttons = 0;
			break;
		case G_MOUSE_MODE_SPIRAL:
			radius = (radius * (8000-sc->sc_tick)) / 8000;
		case G_MOUSE_MODE_CIRCLE:
			/* TODO */
			sc->sc_curr_y_state = 0;
			sc->sc_curr_x_state = 0;
			break;
		case G_MOUSE_MODE_BOX:
			if (sc->sc_tick < 2000) {
				sc->sc_curr_x_state = (sc->sc_tick * radius) / 2000;
				sc->sc_curr_y_state = 0;
			} else if (sc->sc_tick < 4000) {
				sc->sc_curr_x_state = radius;
				sc->sc_curr_y_state = -(((sc->sc_tick - 2000) * radius) / 2000);
			} else if (sc->sc_tick < 6000) {
				sc->sc_curr_x_state = radius - (((sc->sc_tick - 4000) * radius) / 2000);
				sc->sc_curr_y_state = -radius;
			} else {
				sc->sc_curr_x_state = 0;
				sc->sc_curr_y_state = -radius + (((sc->sc_tick - 6000) * radius) / 2000);
			}
			break;
		default:
			break;
		}

		dx = sc->sc_curr_x_state - sc->sc_last_x_state;
		dy = sc->sc_curr_y_state - sc->sc_last_y_state;

		if (dx < -63)
		  dx = -63;
		else if (dx > 63)
		  dx = 63;

		if (dy < -63)
		  dy = -63;
		else if (dy > 63)
		  dy = 63;

		sc->sc_last_x_state += dx;
		sc->sc_last_y_state += dy;

		sc->sc_data.dx = dx;
		sc->sc_data.dy = dy;

		usbd_xfer_set_frame_data(xfer, 0, &sc->sc_data, sizeof(sc->sc_data));
		usbd_xfer_set_frames(xfer, 1);
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		DPRINTF("error=%s\n", usbd_errstr(error));

		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static int
g_mouse_handle_request(device_t dev,
    const void *preq, void **pptr, uint16_t *plen,
    uint16_t offset, uint8_t *pstate)
{
	const struct usb_device_request *req = preq;
	uint8_t is_complete = *pstate;

	if (!is_complete) {
		if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
		    (req->bRequest == UR_SET_PROTOCOL) &&
		    (req->wValue[0] == 0x00) &&
		    (req->wValue[1] == 0x00)) {
			*plen = 0;
			return (0);
		}
	}
	return (ENXIO);			/* use builtin handler */
}
