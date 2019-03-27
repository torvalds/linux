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

#define	USB_DEBUG_VAR g_keyboard_debug
#include <dev/usb/usb_debug.h>

#include <dev/usb/gadget/g_keyboard.h>

static SYSCTL_NODE(_hw_usb, OID_AUTO, g_keyboard, CTLFLAG_RW, 0, "USB keyboard gadget");

#ifdef USB_DEBUG
static int g_keyboard_debug = 0;

SYSCTL_INT(_hw_usb_g_keyboard, OID_AUTO, debug, CTLFLAG_RWTUN,
    &g_keyboard_debug, 0, "Debug level");
#endif

static int g_keyboard_mode = 0;

SYSCTL_INT(_hw_usb_g_keyboard, OID_AUTO, mode, CTLFLAG_RWTUN,
    &g_keyboard_mode, 0, "Mode selection");

static int g_keyboard_key_press_interval = 1000;

SYSCTL_INT(_hw_usb_g_keyboard, OID_AUTO, key_press_interval, CTLFLAG_RWTUN,
    &g_keyboard_key_press_interval, 0, "Key Press Interval in milliseconds");

static char g_keyboard_key_press_pattern[G_KEYBOARD_MAX_STRLEN];

SYSCTL_STRING(_hw_usb_g_keyboard, OID_AUTO, key_press_pattern, CTLFLAG_RW,
    g_keyboard_key_press_pattern, sizeof(g_keyboard_key_press_pattern),
    "Key Press Patterns");

#define	UPROTO_BOOT_KEYBOARD 1

#define	G_KEYBOARD_NMOD                     8	/* units */
#define	G_KEYBOARD_NKEYCODE                 6	/* units */

struct g_keyboard_data {
	uint8_t	modifiers;
#define	MOD_CONTROL_L	0x01
#define	MOD_CONTROL_R	0x10
#define	MOD_SHIFT_L	0x02
#define	MOD_SHIFT_R	0x20
#define	MOD_ALT_L	0x04
#define	MOD_ALT_R	0x40
#define	MOD_WIN_L	0x08
#define	MOD_WIN_R	0x80
	uint8_t	reserved;
	uint8_t	keycode[G_KEYBOARD_NKEYCODE];
};

enum {
	G_KEYBOARD_INTR_DT,
	G_KEYBOARD_N_TRANSFER,
};

struct g_keyboard_softc {
	struct mtx sc_mtx;
	struct usb_callout sc_callout;
	struct g_keyboard_data sc_data[2];
	struct usb_xfer *sc_xfer[G_KEYBOARD_N_TRANSFER];

	int	sc_mode;
	int	sc_state;
	int	sc_pattern_len;

	char	sc_pattern[G_KEYBOARD_MAX_STRLEN];

	uint8_t	sc_led_state[4];
};

static device_probe_t g_keyboard_probe;
static device_attach_t g_keyboard_attach;
static device_detach_t g_keyboard_detach;
static usb_handle_request_t g_keyboard_handle_request;
static usb_callback_t g_keyboard_intr_callback;

static devclass_t g_keyboard_devclass;

static device_method_t g_keyboard_methods[] = {
	/* USB interface */
	DEVMETHOD(usb_handle_request, g_keyboard_handle_request),

	/* Device interface */
	DEVMETHOD(device_probe, g_keyboard_probe),
	DEVMETHOD(device_attach, g_keyboard_attach),
	DEVMETHOD(device_detach, g_keyboard_detach),

	DEVMETHOD_END
};

static driver_t g_keyboard_driver = {
	.name = "g_keyboard",
	.methods = g_keyboard_methods,
	.size = sizeof(struct g_keyboard_softc),
};

DRIVER_MODULE(g_keyboard, uhub, g_keyboard_driver, g_keyboard_devclass, 0, 0);
MODULE_DEPEND(g_keyboard, usb, 1, 1, 1);

static const struct usb_config g_keyboard_config[G_KEYBOARD_N_TRANSFER] = {
	[G_KEYBOARD_INTR_DT] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.ext_buffer = 1,.pipe_bof = 1,},
		.bufsize = sizeof(struct g_keyboard_data),
		.callback = &g_keyboard_intr_callback,
		.frames = 2,
		.usb_mode = USB_MODE_DEVICE,
	},
};

static void g_keyboard_timeout(void *arg);

static void
g_keyboard_timeout_reset(struct g_keyboard_softc *sc)
{
	int i = g_keyboard_key_press_interval;

	if (i <= 0)
		i = 1;
	else if (i > 1023)
		i = 1023;

	i = USB_MS_TO_TICKS(i);

	usb_callout_reset(&sc->sc_callout, i, &g_keyboard_timeout, sc);
}

static void
g_keyboard_timeout(void *arg)
{
	struct g_keyboard_softc *sc = arg;

	sc->sc_mode = g_keyboard_mode;

	memcpy(sc->sc_pattern, g_keyboard_key_press_pattern, sizeof(sc->sc_pattern));

	sc->sc_pattern[G_KEYBOARD_MAX_STRLEN - 1] = 0;

	sc->sc_pattern_len = strlen(sc->sc_pattern);

	DPRINTFN(11, "Timeout %p\n", sc->sc_xfer[G_KEYBOARD_INTR_DT]);

	usbd_transfer_start(sc->sc_xfer[G_KEYBOARD_INTR_DT]);

	g_keyboard_timeout_reset(sc);
}

static int
g_keyboard_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	DPRINTFN(11, "\n");

	if (uaa->usb_mode != USB_MODE_DEVICE)
		return (ENXIO);

	if ((uaa->info.bInterfaceClass == UICLASS_HID) &&
	    (uaa->info.bInterfaceSubClass == UISUBCLASS_BOOT) &&
	    (uaa->info.bInterfaceProtocol == UPROTO_BOOT_KEYBOARD))
		return (0);

	return (ENXIO);
}

static int
g_keyboard_attach(device_t dev)
{
	struct g_keyboard_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	int error;

	DPRINTFN(11, "\n");

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, "g_keyboard", NULL, MTX_DEF);

	usb_callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);

	sc->sc_mode = G_KEYBOARD_MODE_SILENT;

	error = usbd_transfer_setup(uaa->device,
	    &uaa->info.bIfaceIndex, sc->sc_xfer, g_keyboard_config,
	    G_KEYBOARD_N_TRANSFER, sc, &sc->sc_mtx);

	if (error) {
		DPRINTF("error=%s\n", usbd_errstr(error));
		goto detach;
	}
	mtx_lock(&sc->sc_mtx);
	g_keyboard_timeout_reset(sc);
	mtx_unlock(&sc->sc_mtx);

	return (0);			/* success */

detach:
	g_keyboard_detach(dev);

	return (ENXIO);			/* error */
}

static int
g_keyboard_detach(device_t dev)
{
	struct g_keyboard_softc *sc = device_get_softc(dev);

	DPRINTF("\n");

	mtx_lock(&sc->sc_mtx);
	usb_callout_stop(&sc->sc_callout);
	mtx_unlock(&sc->sc_mtx);

	usbd_transfer_unsetup(sc->sc_xfer, G_KEYBOARD_N_TRANSFER);

	usb_callout_drain(&sc->sc_callout);

	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static uint8_t
g_keyboard_get_keycode(struct g_keyboard_softc *sc, int index)
{
	int key;
	int mod = sc->sc_pattern_len;

	if (mod == 0)
		index = 0;
	else
		index %= mod;

	if ((index >= 0) && (index < sc->sc_pattern_len))
		key = sc->sc_pattern[index];
	else
		key = 'a';

	if (key >= 'a' && key <= 'z')
		return (key - 'a' + 0x04);
	else
		return (0x04);
}

static void
g_keyboard_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct g_keyboard_softc *sc = usbd_xfer_softc(xfer);
	int actlen;
	int aframes;

	usbd_xfer_status(xfer, &actlen, NULL, &aframes, NULL);

	DPRINTF("st=%d aframes=%d actlen=%d bytes\n",
	    USB_GET_STATE(xfer), aframes, actlen);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		break;

	case USB_ST_SETUP:
tr_setup:
		if (sc->sc_mode == G_KEYBOARD_MODE_SILENT) {
			memset(&sc->sc_data, 0, sizeof(sc->sc_data));
			usbd_xfer_set_frame_data(xfer, 0, &sc->sc_data[0], sizeof(sc->sc_data[0]));
			usbd_xfer_set_frame_data(xfer, 1, &sc->sc_data[1], sizeof(sc->sc_data[1]));
			usbd_xfer_set_frames(xfer, 2);
			usbd_transfer_submit(xfer);

		} else if (sc->sc_mode == G_KEYBOARD_MODE_PATTERN) {

			memset(&sc->sc_data, 0, sizeof(sc->sc_data));

			if ((sc->sc_state < 0) || (sc->sc_state >= G_KEYBOARD_MAX_STRLEN))
				sc->sc_state = 0;

			switch (sc->sc_state % 6) {
			case 0:
				sc->sc_data[0].keycode[0] =
				    g_keyboard_get_keycode(sc, sc->sc_state + 0);
			case 1:
				sc->sc_data[0].keycode[1] =
				    g_keyboard_get_keycode(sc, sc->sc_state + 1);
			case 2:
				sc->sc_data[0].keycode[2] =
				    g_keyboard_get_keycode(sc, sc->sc_state + 2);
			case 3:
				sc->sc_data[0].keycode[3] =
				    g_keyboard_get_keycode(sc, sc->sc_state + 3);
			case 4:
				sc->sc_data[0].keycode[4] =
				    g_keyboard_get_keycode(sc, sc->sc_state + 4);
			default:
				sc->sc_data[0].keycode[5] =
				    g_keyboard_get_keycode(sc, sc->sc_state + 5);
			}

			sc->sc_state++;

			usbd_xfer_set_frame_data(xfer, 0, &sc->sc_data[0], sizeof(sc->sc_data[0]));
			usbd_xfer_set_frame_data(xfer, 1, &sc->sc_data[1], sizeof(sc->sc_data[1]));
			usbd_xfer_set_frames(xfer, 2);
			usbd_transfer_submit(xfer);
		}
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
g_keyboard_handle_request(device_t dev,
    const void *preq, void **pptr, uint16_t *plen,
    uint16_t offset, uint8_t *pstate)
{
	struct g_keyboard_softc *sc = device_get_softc(dev);
	const struct usb_device_request *req = preq;
	uint8_t is_complete = *pstate;

	if (!is_complete) {
		if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
		    (req->bRequest == UR_SET_REPORT) &&
		    (req->wValue[0] == 0x00) &&
		    (req->wValue[1] == 0x02)) {

			if (offset == 0) {
				*plen = sizeof(sc->sc_led_state);
				*pptr = &sc->sc_led_state;
			} else {
				*plen = 0;
			}
			return (0);
		} else if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
			    (req->bRequest == UR_SET_PROTOCOL) &&
			    (req->wValue[0] == 0x00) &&
		    (req->wValue[1] == 0x00)) {
			*plen = 0;
			return (0);
		} else if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
		    (req->bRequest == UR_SET_IDLE)) {
			*plen = 0;
			return (0);
		}
	}
	return (ENXIO);			/* use builtin handler */
}
