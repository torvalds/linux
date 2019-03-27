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
 * USB audio specs: http://www.usb.org/developers/devclass_docs/audio10.pdf
 *		    http://www.usb.org/developers/devclass_docs/frmts10.pdf
 *		    http://www.usb.org/developers/devclass_docs/termt10.pdf
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

#define	USB_DEBUG_VAR g_audio_debug
#include <dev/usb/usb_debug.h>

#include <dev/usb/gadget/g_audio.h>

enum {
	G_AUDIO_ISOC0_RD,
	G_AUDIO_ISOC1_RD,
	G_AUDIO_ISOC0_WR,
	G_AUDIO_ISOC1_WR,
	G_AUDIO_N_TRANSFER,
};

struct g_audio_softc {
	struct mtx sc_mtx;
	struct usb_callout sc_callout;
	struct usb_callout sc_watchdog;
	struct usb_xfer *sc_xfer[G_AUDIO_N_TRANSFER];

	int	sc_mode;
	int	sc_pattern_len;
	int	sc_throughput;
	int	sc_tx_interval;
	int	sc_state;
	int	sc_noise_rem;

	int8_t	sc_pattern[G_AUDIO_MAX_STRLEN];

	uint16_t sc_data_len[2][G_AUDIO_FRAMES];

	int16_t	sc_data_buf[2][G_AUDIO_BUFSIZE / 2];

	uint8_t	sc_volume_setting[32];
	uint8_t	sc_volume_limit[32];
	uint8_t	sc_sample_rate[32];
};

static SYSCTL_NODE(_hw_usb, OID_AUTO, g_audio, CTLFLAG_RW, 0, "USB audio gadget");

#ifdef USB_DEBUG
static int g_audio_debug = 0;

SYSCTL_INT(_hw_usb_g_audio, OID_AUTO, debug, CTLFLAG_RWTUN,
    &g_audio_debug, 0, "Debug level");
#endif

static int g_audio_mode = 0;

SYSCTL_INT(_hw_usb_g_audio, OID_AUTO, mode, CTLFLAG_RWTUN,
    &g_audio_mode, 0, "Mode selection");

static int g_audio_pattern_interval = 1000;

SYSCTL_INT(_hw_usb_g_audio, OID_AUTO, pattern_interval, CTLFLAG_RWTUN,
    &g_audio_pattern_interval, 0, "Pattern interval in milliseconds");

static char g_audio_pattern_data[G_AUDIO_MAX_STRLEN];

SYSCTL_STRING(_hw_usb_g_audio, OID_AUTO, pattern, CTLFLAG_RW,
    &g_audio_pattern_data, sizeof(g_audio_pattern_data), "Data pattern");

static int g_audio_throughput;

SYSCTL_INT(_hw_usb_g_audio, OID_AUTO, throughput, CTLFLAG_RD,
    &g_audio_throughput, sizeof(g_audio_throughput), "Throughput in bytes per second");

static device_probe_t g_audio_probe;
static device_attach_t g_audio_attach;
static device_detach_t g_audio_detach;
static usb_handle_request_t g_audio_handle_request;

static usb_callback_t g_audio_isoc_read_callback;
static usb_callback_t g_audio_isoc_write_callback;

static devclass_t g_audio_devclass;

static void g_audio_watchdog(void *arg);
static void g_audio_timeout(void *arg);

static device_method_t g_audio_methods[] = {
	/* USB interface */
	DEVMETHOD(usb_handle_request, g_audio_handle_request),

	/* Device interface */
	DEVMETHOD(device_probe, g_audio_probe),
	DEVMETHOD(device_attach, g_audio_attach),
	DEVMETHOD(device_detach, g_audio_detach),

	DEVMETHOD_END
};

static driver_t g_audio_driver = {
	.name = "g_audio",
	.methods = g_audio_methods,
	.size = sizeof(struct g_audio_softc),
};

DRIVER_MODULE(g_audio, uhub, g_audio_driver, g_audio_devclass, 0, 0);
MODULE_DEPEND(g_audio, usb, 1, 1, 1);

static const struct usb_config g_audio_config[G_AUDIO_N_TRANSFER] = {

	[G_AUDIO_ISOC0_RD] = {
		.type = UE_ISOCHRONOUS,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_RX,
		.flags = {.ext_buffer = 1,.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = G_AUDIO_BUFSIZE,
		.callback = &g_audio_isoc_read_callback,
		.frames = G_AUDIO_FRAMES,
		.usb_mode = USB_MODE_DEVICE,
		.if_index = 1,
	},

	[G_AUDIO_ISOC1_RD] = {
		.type = UE_ISOCHRONOUS,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_RX,
		.flags = {.ext_buffer = 1,.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = G_AUDIO_BUFSIZE,
		.callback = &g_audio_isoc_read_callback,
		.frames = G_AUDIO_FRAMES,
		.usb_mode = USB_MODE_DEVICE,
		.if_index = 1,
	},

	[G_AUDIO_ISOC0_WR] = {
		.type = UE_ISOCHRONOUS,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.flags = {.ext_buffer = 1,.pipe_bof = 1,},
		.bufsize = G_AUDIO_BUFSIZE,
		.callback = &g_audio_isoc_write_callback,
		.frames = G_AUDIO_FRAMES,
		.usb_mode = USB_MODE_DEVICE,
		.if_index = 2,
	},

	[G_AUDIO_ISOC1_WR] = {
		.type = UE_ISOCHRONOUS,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.flags = {.ext_buffer = 1,.pipe_bof = 1,},
		.bufsize = G_AUDIO_BUFSIZE,
		.callback = &g_audio_isoc_write_callback,
		.frames = G_AUDIO_FRAMES,
		.usb_mode = USB_MODE_DEVICE,
		.if_index = 2,
	},
};

static void
g_audio_timeout_reset(struct g_audio_softc *sc)
{
	int i = g_audio_pattern_interval;

	sc->sc_tx_interval = i;

	if (i <= 0)
		i = 1;
	else if (i > 1023)
		i = 1023;

	i = USB_MS_TO_TICKS(i);

	usb_callout_reset(&sc->sc_callout, i, &g_audio_timeout, sc);
}

static void
g_audio_timeout(void *arg)
{
	struct g_audio_softc *sc = arg;

	sc->sc_mode = g_audio_mode;

	memcpy(sc->sc_pattern, g_audio_pattern_data, sizeof(sc->sc_pattern));

	sc->sc_pattern[G_AUDIO_MAX_STRLEN - 1] = 0;

	sc->sc_pattern_len = strlen(sc->sc_pattern);

	if (sc->sc_mode != G_AUDIO_MODE_LOOP) {
		usbd_transfer_start(sc->sc_xfer[G_AUDIO_ISOC0_WR]);
		usbd_transfer_start(sc->sc_xfer[G_AUDIO_ISOC1_WR]);
	}
	g_audio_timeout_reset(sc);
}

static void
g_audio_watchdog_reset(struct g_audio_softc *sc)
{
	usb_callout_reset(&sc->sc_watchdog, hz, &g_audio_watchdog, sc);
}

static void
g_audio_watchdog(void *arg)
{
	struct g_audio_softc *sc = arg;
	int i;

	i = sc->sc_throughput;

	sc->sc_throughput = 0;

	g_audio_throughput = i;

	g_audio_watchdog_reset(sc);
}

static int
g_audio_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	DPRINTFN(11, "\n");

	if (uaa->usb_mode != USB_MODE_DEVICE)
		return (ENXIO);

	if ((uaa->info.bInterfaceClass == UICLASS_AUDIO) &&
	    (uaa->info.bInterfaceSubClass == UISUBCLASS_AUDIOCONTROL))
		return (0);

	return (ENXIO);
}

static int
g_audio_attach(device_t dev)
{
	struct g_audio_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	int error;
	int i;
	uint8_t iface_index[3];

	DPRINTFN(11, "\n");

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, "g_audio", NULL, MTX_DEF);

	usb_callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);
	usb_callout_init_mtx(&sc->sc_watchdog, &sc->sc_mtx, 0);

	sc->sc_mode = G_AUDIO_MODE_SILENT;

	sc->sc_noise_rem = 1;

	for (i = 0; i != G_AUDIO_FRAMES; i++) {
		sc->sc_data_len[0][i] = G_AUDIO_BUFSIZE / G_AUDIO_FRAMES;
		sc->sc_data_len[1][i] = G_AUDIO_BUFSIZE / G_AUDIO_FRAMES;
	}

	iface_index[0] = uaa->info.bIfaceIndex;
	iface_index[1] = uaa->info.bIfaceIndex + 1;
	iface_index[2] = uaa->info.bIfaceIndex + 2;

	error = usbd_set_alt_interface_index(uaa->device, iface_index[1], 1);
	if (error) {
		DPRINTF("alt iface setting error=%s\n", usbd_errstr(error));
		goto detach;
	}
	error = usbd_set_alt_interface_index(uaa->device, iface_index[2], 1);
	if (error) {
		DPRINTF("alt iface setting error=%s\n", usbd_errstr(error));
		goto detach;
	}
	error = usbd_transfer_setup(uaa->device,
	    iface_index, sc->sc_xfer, g_audio_config,
	    G_AUDIO_N_TRANSFER, sc, &sc->sc_mtx);

	if (error) {
		DPRINTF("error=%s\n", usbd_errstr(error));
		goto detach;
	}
	usbd_set_parent_iface(uaa->device, iface_index[1], iface_index[0]);
	usbd_set_parent_iface(uaa->device, iface_index[2], iface_index[0]);

	mtx_lock(&sc->sc_mtx);

	usbd_transfer_start(sc->sc_xfer[G_AUDIO_ISOC0_RD]);
	usbd_transfer_start(sc->sc_xfer[G_AUDIO_ISOC1_RD]);

	usbd_transfer_start(sc->sc_xfer[G_AUDIO_ISOC0_WR]);
	usbd_transfer_start(sc->sc_xfer[G_AUDIO_ISOC1_WR]);

	g_audio_timeout_reset(sc);

	g_audio_watchdog_reset(sc);

	mtx_unlock(&sc->sc_mtx);

	return (0);			/* success */

detach:
	g_audio_detach(dev);

	return (ENXIO);			/* error */
}

static int
g_audio_detach(device_t dev)
{
	struct g_audio_softc *sc = device_get_softc(dev);

	DPRINTF("\n");

	mtx_lock(&sc->sc_mtx);
	usb_callout_stop(&sc->sc_callout);
	usb_callout_stop(&sc->sc_watchdog);
	mtx_unlock(&sc->sc_mtx);

	usbd_transfer_unsetup(sc->sc_xfer, G_AUDIO_N_TRANSFER);

	usb_callout_drain(&sc->sc_callout);
	usb_callout_drain(&sc->sc_watchdog);

	mtx_destroy(&sc->sc_mtx);

	return (0);
}


static int32_t
g_noise(struct g_audio_softc *sc)
{
	uint32_t temp;
	const uint32_t prime = 0xFFFF1D;

	if (sc->sc_noise_rem & 1) {
		sc->sc_noise_rem += prime;
	}
	sc->sc_noise_rem /= 2;

	temp = sc->sc_noise_rem;

	/* unsigned to signed conversion */

	temp ^= 0x800000;
	if (temp & 0x800000) {
		temp |= (-0x800000);
	}
	return temp;
}

static void
g_audio_make_samples(struct g_audio_softc *sc, int16_t *ptr, int samples)
{
	int i;
	int j;

	for (i = 0; i != samples; i++) {

		j = g_noise(sc);

		if ((sc->sc_state < 0) || (sc->sc_state >= sc->sc_pattern_len))
			sc->sc_state = 0;

		if (sc->sc_pattern_len != 0) {
			j = (j * sc->sc_pattern[sc->sc_state]) >> 16;
			sc->sc_state++;
		}
		*ptr++ = j / 256;
		*ptr++ = j / 256;
	}
}

static void
g_audio_isoc_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct g_audio_softc *sc = usbd_xfer_softc(xfer);
	int actlen;
	int aframes;
	int nr = (xfer == sc->sc_xfer[G_AUDIO_ISOC0_WR]) ? 0 : 1;
	int16_t *ptr;
	int i;

	usbd_xfer_status(xfer, &actlen, NULL, &aframes, NULL);

	DPRINTF("st=%d aframes=%d actlen=%d bytes\n",
	    USB_GET_STATE(xfer), aframes, actlen);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		sc->sc_throughput += actlen;

		if (sc->sc_mode == G_AUDIO_MODE_LOOP)
			break;		/* sync with RX */

	case USB_ST_SETUP:
tr_setup:

		ptr = sc->sc_data_buf[nr];

		if (sc->sc_mode == G_AUDIO_MODE_PATTERN) {

			for (i = 0; i != G_AUDIO_FRAMES; i++) {

				usbd_xfer_set_frame_data(xfer, i, ptr, sc->sc_data_len[nr][i]);

				g_audio_make_samples(sc, ptr, (G_AUDIO_BUFSIZE / G_AUDIO_FRAMES) / 2);

				ptr += (G_AUDIO_BUFSIZE / G_AUDIO_FRAMES) / 2;
			}
		} else if (sc->sc_mode == G_AUDIO_MODE_LOOP) {

			for (i = 0; i != G_AUDIO_FRAMES; i++) {

				usbd_xfer_set_frame_data(xfer, i, ptr, sc->sc_data_len[nr][i] & ~3);

				g_audio_make_samples(sc, ptr, sc->sc_data_len[nr][i] / 4);

				ptr += (G_AUDIO_BUFSIZE / G_AUDIO_FRAMES) / 2;
			}
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
g_audio_isoc_read_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct g_audio_softc *sc = usbd_xfer_softc(xfer);
	int actlen;
	int aframes;
	int nr = (xfer == sc->sc_xfer[G_AUDIO_ISOC0_RD]) ? 0 : 1;
	int16_t *ptr;
	int i;

	usbd_xfer_status(xfer, &actlen, NULL, &aframes, NULL);

	DPRINTF("st=%d aframes=%d actlen=%d bytes\n",
	    USB_GET_STATE(xfer), aframes, actlen);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		sc->sc_throughput += actlen;

		for (i = 0; i != G_AUDIO_FRAMES; i++) {
			sc->sc_data_len[nr][i] = usbd_xfer_frame_len(xfer, i);
		}

		usbd_transfer_start(sc->sc_xfer[G_AUDIO_ISOC0_WR]);
		usbd_transfer_start(sc->sc_xfer[G_AUDIO_ISOC1_WR]);

		break;

	case USB_ST_SETUP:
tr_setup:
		ptr = sc->sc_data_buf[nr];

		for (i = 0; i != G_AUDIO_FRAMES; i++) {

			usbd_xfer_set_frame_data(xfer, i, ptr,
			    G_AUDIO_BUFSIZE / G_AUDIO_FRAMES);

			ptr += (G_AUDIO_BUFSIZE / G_AUDIO_FRAMES) / 2;
		}

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
g_audio_handle_request(device_t dev,
    const void *preq, void **pptr, uint16_t *plen,
    uint16_t offset, uint8_t *pstate)
{
	struct g_audio_softc *sc = device_get_softc(dev);
	const struct usb_device_request *req = preq;
	uint8_t is_complete = *pstate;

	if (!is_complete) {
		if ((req->bmRequestType == UT_READ_CLASS_INTERFACE) &&
		    (req->bRequest == 0x82 /* get min */ )) {

			if (offset == 0) {
				USETW(sc->sc_volume_limit, 0);
				*plen = 2;
				*pptr = &sc->sc_volume_limit;
			} else {
				*plen = 0;
			}
			return (0);
		} else if ((req->bmRequestType == UT_READ_CLASS_INTERFACE) &&
		    (req->bRequest == 0x83 /* get max */ )) {

			if (offset == 0) {
				USETW(sc->sc_volume_limit, 0x2000);
				*plen = 2;
				*pptr = &sc->sc_volume_limit;
			} else {
				*plen = 0;
			}
			return (0);
		} else if ((req->bmRequestType == UT_READ_CLASS_INTERFACE) &&
		    (req->bRequest == 0x84 /* get residue */ )) {

			if (offset == 0) {
				USETW(sc->sc_volume_limit, 1);
				*plen = 2;
				*pptr = &sc->sc_volume_limit;
			} else {
				*plen = 0;
			}
			return (0);
		} else if ((req->bmRequestType == UT_READ_CLASS_INTERFACE) &&
		    (req->bRequest == 0x81 /* get value */ )) {

			if (offset == 0) {
				USETW(sc->sc_volume_setting, 0x2000);
				*plen = sizeof(sc->sc_volume_setting);
				*pptr = &sc->sc_volume_setting;
			} else {
				*plen = 0;
			}
			return (0);
		} else if ((req->bmRequestType == UT_WRITE_CLASS_INTERFACE) &&
		    (req->bRequest == 0x01 /* set value */ )) {

			if (offset == 0) {
				*plen = sizeof(sc->sc_volume_setting);
				*pptr = &sc->sc_volume_setting;
			} else {
				*plen = 0;
			}
			return (0);
		} else if ((req->bmRequestType == UT_WRITE_CLASS_ENDPOINT) &&
		    (req->bRequest == 0x01 /* set value */ )) {

			if (offset == 0) {
				*plen = sizeof(sc->sc_sample_rate);
				*pptr = &sc->sc_sample_rate;
			} else {
				*plen = 0;
			}
			return (0);
		}
	}
	return (ENXIO);			/* use builtin handler */
}
