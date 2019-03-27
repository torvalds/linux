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
 * Comm Class spec:  http://www.usb.org/developers/devclass_docs/usbccs10.pdf
 *                   http://www.usb.org/developers/devclass_docs/usbcdc11.pdf
 *                   http://www.usb.org/developers/devclass_docs/cdc_wmc10.zip
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
#include <dev/usb/usb_cdc.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbhid.h>
#include "usb_if.h"

#define	USB_DEBUG_VAR g_modem_debug
#include <dev/usb/usb_debug.h>

#include <dev/usb/gadget/g_modem.h>

enum {
	G_MODEM_INTR_DT,
	G_MODEM_BULK_RD,
	G_MODEM_BULK_WR,
	G_MODEM_N_TRANSFER,
};

struct g_modem_softc {
	struct mtx sc_mtx;
	struct usb_callout sc_callout;
	struct usb_callout sc_watchdog;
	struct usb_xfer *sc_xfer[G_MODEM_N_TRANSFER];

	int	sc_mode;
	int	sc_tx_busy;
	int	sc_pattern_len;
	int	sc_throughput;
	int	sc_tx_interval;

	char	sc_pattern[G_MODEM_MAX_STRLEN];

	uint16_t sc_data_len;

	uint8_t sc_data_buf[G_MODEM_BUFSIZE];
	uint8_t	sc_line_coding[32];
	uint8_t	sc_abstract_state[32];
};

static SYSCTL_NODE(_hw_usb, OID_AUTO, g_modem, CTLFLAG_RW, 0, "USB modem gadget");

#ifdef USB_DEBUG
static int g_modem_debug = 0;

SYSCTL_INT(_hw_usb_g_modem, OID_AUTO, debug, CTLFLAG_RWTUN,
    &g_modem_debug, 0, "Debug level");
#endif

static int g_modem_mode = 0;

SYSCTL_INT(_hw_usb_g_modem, OID_AUTO, mode, CTLFLAG_RWTUN,
    &g_modem_mode, 0, "Mode selection");

static int g_modem_pattern_interval = 1000;

SYSCTL_INT(_hw_usb_g_modem, OID_AUTO, pattern_interval, CTLFLAG_RWTUN,
    &g_modem_pattern_interval, 0, "Pattern interval in milliseconds");

static char g_modem_pattern_data[G_MODEM_MAX_STRLEN];

SYSCTL_STRING(_hw_usb_g_modem, OID_AUTO, pattern, CTLFLAG_RW,
    &g_modem_pattern_data, sizeof(g_modem_pattern_data), "Data pattern");

static int g_modem_throughput;

SYSCTL_INT(_hw_usb_g_modem, OID_AUTO, throughput, CTLFLAG_RD,
    &g_modem_throughput, sizeof(g_modem_throughput), "Throughput in bytes per second");

static device_probe_t g_modem_probe;
static device_attach_t g_modem_attach;
static device_detach_t g_modem_detach;
static usb_handle_request_t g_modem_handle_request;
static usb_callback_t g_modem_intr_callback;
static usb_callback_t g_modem_bulk_read_callback;
static usb_callback_t g_modem_bulk_write_callback;

static void g_modem_timeout(void *arg);

static devclass_t g_modem_devclass;

static device_method_t g_modem_methods[] = {
	/* USB interface */
	DEVMETHOD(usb_handle_request, g_modem_handle_request),

	/* Device interface */
	DEVMETHOD(device_probe, g_modem_probe),
	DEVMETHOD(device_attach, g_modem_attach),
	DEVMETHOD(device_detach, g_modem_detach),

	DEVMETHOD_END
};

static driver_t g_modem_driver = {
	.name = "g_modem",
	.methods = g_modem_methods,
	.size = sizeof(struct g_modem_softc),
};

DRIVER_MODULE(g_modem, uhub, g_modem_driver, g_modem_devclass, 0, 0);
MODULE_DEPEND(g_modem, usb, 1, 1, 1);

static const struct usb_config g_modem_config[G_MODEM_N_TRANSFER] = {

	[G_MODEM_INTR_DT] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.flags = {.ext_buffer = 1,.pipe_bof = 1,},
		.bufsize = 0,	/* use wMaxPacketSize */
		.callback = &g_modem_intr_callback,
		.frames = 1,
		.usb_mode = USB_MODE_DEVICE,
		.if_index = 0,
	},

	[G_MODEM_BULK_RD] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_RX,
		.flags = {.ext_buffer = 1,.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = G_MODEM_BUFSIZE,
		.callback = &g_modem_bulk_read_callback,
		.frames = 1,
		.usb_mode = USB_MODE_DEVICE,
		.if_index = 1,
	},

	[G_MODEM_BULK_WR] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.flags = {.ext_buffer = 1,.pipe_bof = 1,},
		.bufsize = G_MODEM_BUFSIZE,
		.callback = &g_modem_bulk_write_callback,
		.frames = 1,
		.usb_mode = USB_MODE_DEVICE,
		.if_index = 1,
	},
};

static void
g_modem_timeout_reset(struct g_modem_softc *sc)
{
	int i = g_modem_pattern_interval;

	sc->sc_tx_interval = i;

	if (i <= 0)
		i = 1;
	else if (i > 1023)
		i = 1023;

	i = USB_MS_TO_TICKS(i);

	usb_callout_reset(&sc->sc_callout, i, &g_modem_timeout, sc);
}

static void
g_modem_timeout(void *arg)
{
	struct g_modem_softc *sc = arg;

	sc->sc_mode = g_modem_mode;

	memcpy(sc->sc_pattern, g_modem_pattern_data, sizeof(sc->sc_pattern));

	sc->sc_pattern[G_MODEM_MAX_STRLEN - 1] = 0;

	sc->sc_pattern_len = strlen(sc->sc_pattern);

	DPRINTFN(11, "Timeout %p\n", sc->sc_xfer[G_MODEM_INTR_DT]);

	usbd_transfer_start(sc->sc_xfer[G_MODEM_BULK_WR]);
	usbd_transfer_start(sc->sc_xfer[G_MODEM_BULK_RD]);

	g_modem_timeout_reset(sc);
}

static void g_modem_watchdog(void *arg);

static void
g_modem_watchdog_reset(struct g_modem_softc *sc)
{
	usb_callout_reset(&sc->sc_watchdog, hz, &g_modem_watchdog, sc);
}

static void
g_modem_watchdog(void *arg)
{
	struct g_modem_softc *sc = arg;
	int i;

	i = sc->sc_throughput;

	sc->sc_throughput = 0;

	g_modem_throughput = i;

	g_modem_watchdog_reset(sc);
}

static int
g_modem_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	DPRINTFN(11, "\n");

	if (uaa->usb_mode != USB_MODE_DEVICE)
		return (ENXIO);

	if ((uaa->info.bInterfaceClass == UICLASS_CDC) &&
	    (uaa->info.bInterfaceSubClass == UISUBCLASS_ABSTRACT_CONTROL_MODEL) &&
	    (uaa->info.bInterfaceProtocol == UIPROTO_CDC_AT))
		return (0);

	return (ENXIO);
}

static int
g_modem_attach(device_t dev)
{
	struct g_modem_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	int error;
	uint8_t iface_index[2];

	DPRINTFN(11, "\n");

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, "g_modem", NULL, MTX_DEF);

	usb_callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);
	usb_callout_init_mtx(&sc->sc_watchdog, &sc->sc_mtx, 0);

	sc->sc_mode = G_MODEM_MODE_SILENT;

	iface_index[0] = uaa->info.bIfaceIndex;
	iface_index[1] = uaa->info.bIfaceIndex + 1;

	error = usbd_transfer_setup(uaa->device,
	    iface_index, sc->sc_xfer, g_modem_config,
	    G_MODEM_N_TRANSFER, sc, &sc->sc_mtx);

	if (error) {
		DPRINTF("error=%s\n", usbd_errstr(error));
		goto detach;
	}
	usbd_set_parent_iface(uaa->device, iface_index[1], iface_index[0]);

	mtx_lock(&sc->sc_mtx);
	g_modem_timeout_reset(sc);
	g_modem_watchdog_reset(sc);
	mtx_unlock(&sc->sc_mtx);

	return (0);			/* success */

detach:
	g_modem_detach(dev);

	return (ENXIO);			/* error */
}

static int
g_modem_detach(device_t dev)
{
	struct g_modem_softc *sc = device_get_softc(dev);

	DPRINTF("\n");

	mtx_lock(&sc->sc_mtx);
	usb_callout_stop(&sc->sc_callout);
	usb_callout_stop(&sc->sc_watchdog);
	mtx_unlock(&sc->sc_mtx);

	usbd_transfer_unsetup(sc->sc_xfer, G_MODEM_N_TRANSFER);

	usb_callout_drain(&sc->sc_callout);
	usb_callout_drain(&sc->sc_watchdog);

	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static void
g_modem_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
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

static void
g_modem_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct g_modem_softc *sc = usbd_xfer_softc(xfer);
	int actlen;
	int aframes;
	int mod;
	int x;
	int max;

	usbd_xfer_status(xfer, &actlen, NULL, &aframes, NULL);

	DPRINTF("st=%d aframes=%d actlen=%d bytes\n",
	    USB_GET_STATE(xfer), aframes, actlen);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		sc->sc_tx_busy = 0;
		sc->sc_throughput += actlen;

		if (sc->sc_mode == G_MODEM_MODE_LOOP) {
			/* start loop */
			usbd_transfer_start(sc->sc_xfer[G_MODEM_BULK_RD]);
			break;
		} else if ((sc->sc_mode == G_MODEM_MODE_PATTERN) && (sc->sc_tx_interval != 0)) {
			/* wait for next timeout */
			break;
		}
	case USB_ST_SETUP:
tr_setup:
		if (sc->sc_mode == G_MODEM_MODE_PATTERN) {

			mod = sc->sc_pattern_len;
			max = sc->sc_tx_interval ? mod : G_MODEM_BUFSIZE;

			if (mod == 0) {
				for (x = 0; x != max; x++)
					sc->sc_data_buf[x] = x % 255;
			} else {
				for (x = 0; x != max; x++)
					sc->sc_data_buf[x] = sc->sc_pattern[x % mod];
			}

			usbd_xfer_set_frame_data(xfer, 0, sc->sc_data_buf, max);
			usbd_xfer_set_interval(xfer, 0);
			usbd_xfer_set_frames(xfer, 1);
			usbd_transfer_submit(xfer);

		} else if (sc->sc_mode == G_MODEM_MODE_LOOP) {

			if (sc->sc_tx_busy == 0)
				break;

			x = sc->sc_tx_interval;

			if (x < 0)
				x = 0;
			else if (x > 256)
				x = 256;

			usbd_xfer_set_frame_data(xfer, 0, sc->sc_data_buf, sc->sc_data_len);
			usbd_xfer_set_interval(xfer, x);
			usbd_xfer_set_frames(xfer, 1);
			usbd_transfer_submit(xfer);
		} else {
			sc->sc_tx_busy = 0;
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

static void
g_modem_bulk_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct g_modem_softc *sc = usbd_xfer_softc(xfer);
	int actlen;
	int aframes;

	usbd_xfer_status(xfer, &actlen, NULL, &aframes, NULL);

	DPRINTF("st=%d aframes=%d actlen=%d bytes\n",
	    USB_GET_STATE(xfer), aframes, actlen);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		sc->sc_throughput += actlen;

		if (sc->sc_mode == G_MODEM_MODE_LOOP) {
			sc->sc_tx_busy = 1;
			sc->sc_data_len = actlen;
			usbd_transfer_start(sc->sc_xfer[G_MODEM_BULK_WR]);
			break;
		}

	case USB_ST_SETUP:
tr_setup:
		if ((sc->sc_mode == G_MODEM_MODE_SILENT) ||
		    (sc->sc_tx_busy != 0))
			break;

		usbd_xfer_set_frame_data(xfer, 0, sc->sc_data_buf, G_MODEM_BUFSIZE);
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
g_modem_handle_request(device_t dev,
    const void *preq, void **pptr, uint16_t *plen,
    uint16_t offset, uint8_t *pstate)
{
	struct g_modem_softc *sc = device_get_softc(dev);
	const struct usb_device_request *req = preq;
	uint8_t is_complete = *pstate;

	if (!is_complete) {
		if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
		    (req->bRequest == UCDC_SET_LINE_CODING) &&
		    (req->wValue[0] == 0x00) &&
		    (req->wValue[1] == 0x00)) {

			if (offset == 0) {
				*plen = sizeof(sc->sc_line_coding);
				*pptr = &sc->sc_line_coding;
			} else {
				*plen = 0;
			}
			return (0);
		} else if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
		    (req->bRequest == UCDC_SET_COMM_FEATURE)) {

			if (offset == 0) {
				*plen = sizeof(sc->sc_abstract_state);
				*pptr = &sc->sc_abstract_state;
			} else {
				*plen = 0;
			}
			return (0);
		} else if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
		    (req->bRequest == UCDC_SET_CONTROL_LINE_STATE)) {
			*plen = 0;
			return (0);
		} else if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
		    (req->bRequest == UCDC_SEND_BREAK)) {
			*plen = 0;
			return (0);
		}
	}
	return (ENXIO);			/* use builtin handler */
}
