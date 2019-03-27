/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * HID spec: http://www.usb.org/developers/devclass_docs/HID1_11.pdf
 */

#include "opt_evdev.h"

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
#include <sys/sbuf.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbhid.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR ums_debug
#include <dev/usb/usb_debug.h>

#include <dev/usb/quirk/usb_quirk.h>

#ifdef EVDEV_SUPPORT
#include <dev/evdev/input.h>
#include <dev/evdev/evdev.h>
#endif

#include <sys/ioccom.h>
#include <sys/filio.h>
#include <sys/tty.h>
#include <sys/mouse.h>

#ifdef USB_DEBUG
static int ums_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, ums, CTLFLAG_RW, 0, "USB ums");
SYSCTL_INT(_hw_usb_ums, OID_AUTO, debug, CTLFLAG_RWTUN,
    &ums_debug, 0, "Debug level");
#endif

#define	MOUSE_FLAGS_MASK (HIO_CONST|HIO_RELATIVE)
#define	MOUSE_FLAGS (HIO_RELATIVE)

#define	UMS_BUF_SIZE      8		/* bytes */
#define	UMS_IFQ_MAXLEN   50		/* units */
#define	UMS_BUTTON_MAX   31		/* exclusive, must be less than 32 */
#define	UMS_BUT(i) ((i) < 3 ? (((i) + 2) % 3) : (i))
#define	UMS_INFO_MAX	  2		/* maximum number of HID sets */

enum {
	UMS_INTR_DT,
	UMS_N_TRANSFER,
};

struct ums_info {
	struct hid_location sc_loc_w;
	struct hid_location sc_loc_x;
	struct hid_location sc_loc_y;
	struct hid_location sc_loc_z;
	struct hid_location sc_loc_t;
	struct hid_location sc_loc_btn[UMS_BUTTON_MAX];

	uint32_t sc_flags;
#define	UMS_FLAG_X_AXIS     0x0001
#define	UMS_FLAG_Y_AXIS     0x0002
#define	UMS_FLAG_Z_AXIS     0x0004
#define	UMS_FLAG_T_AXIS     0x0008
#define	UMS_FLAG_SBU        0x0010	/* spurious button up events */
#define	UMS_FLAG_REVZ	    0x0020	/* Z-axis is reversed */
#define	UMS_FLAG_W_AXIS     0x0040

	uint8_t	sc_iid_w;
	uint8_t	sc_iid_x;
	uint8_t	sc_iid_y;
	uint8_t	sc_iid_z;
	uint8_t	sc_iid_t;
	uint8_t	sc_iid_btn[UMS_BUTTON_MAX];
	uint8_t	sc_buttons;
};

struct ums_softc {
	struct usb_fifo_sc sc_fifo;
	struct mtx sc_mtx;
	struct usb_callout sc_callout;
	struct ums_info sc_info[UMS_INFO_MAX];

	mousehw_t sc_hw;
	mousemode_t sc_mode;
	mousestatus_t sc_status;

	struct usb_xfer *sc_xfer[UMS_N_TRANSFER];

	int sc_pollrate;
	int sc_fflags;
#ifdef EVDEV_SUPPORT
	int sc_evflags;
#define	UMS_EVDEV_OPENED	1
#endif

	uint8_t	sc_buttons;
	uint8_t	sc_iid;
	uint8_t	sc_temp[64];

#ifdef EVDEV_SUPPORT
	struct evdev_dev *sc_evdev;
#endif
};

static void ums_put_queue_timeout(void *__sc);

static usb_callback_t ums_intr_callback;

static device_probe_t ums_probe;
static device_attach_t ums_attach;
static device_detach_t ums_detach;

static usb_fifo_cmd_t ums_fifo_start_read;
static usb_fifo_cmd_t ums_fifo_stop_read;
static usb_fifo_open_t ums_fifo_open;
static usb_fifo_close_t ums_fifo_close;
static usb_fifo_ioctl_t ums_fifo_ioctl;

#ifdef EVDEV_SUPPORT
static evdev_open_t ums_ev_open;
static evdev_close_t ums_ev_close;
static void ums_evdev_push(struct ums_softc *, int32_t, int32_t,
    int32_t, int32_t, int32_t);
#endif

static void	ums_start_rx(struct ums_softc *);
static void	ums_stop_rx(struct ums_softc *);
static void	ums_put_queue(struct ums_softc *, int32_t, int32_t,
		    int32_t, int32_t, int32_t);
static int	ums_sysctl_handler_parseinfo(SYSCTL_HANDLER_ARGS);

static struct usb_fifo_methods ums_fifo_methods = {
	.f_open = &ums_fifo_open,
	.f_close = &ums_fifo_close,
	.f_ioctl = &ums_fifo_ioctl,
	.f_start_read = &ums_fifo_start_read,
	.f_stop_read = &ums_fifo_stop_read,
	.basename[0] = "ums",
};

#ifdef EVDEV_SUPPORT
static const struct evdev_methods ums_evdev_methods = {
	.ev_open = &ums_ev_open,
	.ev_close = &ums_ev_close,
};
#endif

static void
ums_put_queue_timeout(void *__sc)
{
	struct ums_softc *sc = __sc;

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	ums_put_queue(sc, 0, 0, 0, 0, 0);
#ifdef EVDEV_SUPPORT
	ums_evdev_push(sc, 0, 0, 0, 0, 0);
#endif
}

static void
ums_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ums_softc *sc = usbd_xfer_softc(xfer);
	struct ums_info *info = &sc->sc_info[0];
	struct usb_page_cache *pc;
	uint8_t *buf = sc->sc_temp;
	int32_t buttons = 0;
	int32_t buttons_found = 0;
#ifdef EVDEV_SUPPORT
	int32_t buttons_reported = 0;
#endif
	int32_t dw = 0;
	int32_t dx = 0;
	int32_t dy = 0;
	int32_t dz = 0;
	int32_t dt = 0;
	uint8_t i;
	uint8_t id;
	int len;

	usbd_xfer_status(xfer, &len, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(6, "sc=%p actlen=%d\n", sc, len);

		if (len > (int)sizeof(sc->sc_temp)) {
			DPRINTFN(6, "truncating large packet to %zu bytes\n",
			    sizeof(sc->sc_temp));
			len = sizeof(sc->sc_temp);
		}
		if (len == 0)
			goto tr_setup;

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, buf, len);

		DPRINTFN(6, "data = %02x %02x %02x %02x "
		    "%02x %02x %02x %02x\n",
		    (len > 0) ? buf[0] : 0, (len > 1) ? buf[1] : 0,
		    (len > 2) ? buf[2] : 0, (len > 3) ? buf[3] : 0,
		    (len > 4) ? buf[4] : 0, (len > 5) ? buf[5] : 0,
		    (len > 6) ? buf[6] : 0, (len > 7) ? buf[7] : 0);

		if (sc->sc_iid) {
			id = *buf;

			len--;
			buf++;

		} else {
			id = 0;
			if (sc->sc_info[0].sc_flags & UMS_FLAG_SBU) {
				if ((*buf == 0x14) || (*buf == 0x15)) {
					goto tr_setup;
				}
			}
		}

	repeat:
		if ((info->sc_flags & UMS_FLAG_W_AXIS) &&
		    (id == info->sc_iid_w))
			dw += hid_get_data(buf, len, &info->sc_loc_w);

		if ((info->sc_flags & UMS_FLAG_X_AXIS) && 
		    (id == info->sc_iid_x))
			dx += hid_get_data(buf, len, &info->sc_loc_x);

		if ((info->sc_flags & UMS_FLAG_Y_AXIS) &&
		    (id == info->sc_iid_y))
			dy -= hid_get_data(buf, len, &info->sc_loc_y);

		if ((info->sc_flags & UMS_FLAG_Z_AXIS) &&
		    (id == info->sc_iid_z)) {
			int32_t temp;
			temp = hid_get_data(buf, len, &info->sc_loc_z);
			if (info->sc_flags & UMS_FLAG_REVZ)
				temp = -temp;
			dz -= temp;
		}

		if ((info->sc_flags & UMS_FLAG_T_AXIS) &&
		    (id == info->sc_iid_t)) {
			dt += hid_get_data(buf, len, &info->sc_loc_t);
			/* T-axis is translated into button presses */
			buttons_found |= (1UL << 5) | (1UL << 6);
		}

		for (i = 0; i < info->sc_buttons; i++) {
			uint32_t mask;
			mask = 1UL << UMS_BUT(i);
			/* check for correct button ID */
			if (id != info->sc_iid_btn[i])
				continue;
			/* check for button pressed */
			if (hid_get_data(buf, len, &info->sc_loc_btn[i]))
				buttons |= mask;
			/* register button mask */
			buttons_found |= mask;
		}

		if (++info != &sc->sc_info[UMS_INFO_MAX])
			goto repeat;

#ifdef EVDEV_SUPPORT
		buttons_reported = buttons;
#endif
		/* keep old button value(s) for non-detected buttons */
		buttons |= sc->sc_status.button & ~buttons_found;

		if (dx || dy || dz || dt || dw ||
		    (buttons != sc->sc_status.button)) {

			DPRINTFN(6, "x:%d y:%d z:%d t:%d w:%d buttons:0x%08x\n",
			    dx, dy, dz, dt, dw, buttons);

			/* translate T-axis into button presses until further */
			if (dt > 0) {
				ums_put_queue(sc, 0, 0, 0, 0, buttons);
				buttons |= 1UL << 6;
			} else if (dt < 0) {
				ums_put_queue(sc, 0, 0, 0, 0, buttons);
				buttons |= 1UL << 5;
			}

			sc->sc_status.button = buttons;
			sc->sc_status.dx += dx;
			sc->sc_status.dy += dy;
			sc->sc_status.dz += dz;
			/*
			 * sc->sc_status.dt += dt;
			 * no way to export this yet
			 */

			/*
		         * The Qtronix keyboard has a built in PS/2
		         * port for a mouse.  The firmware once in a
		         * while posts a spurious button up
		         * event. This event we ignore by doing a
		         * timeout for 50 msecs.  If we receive
		         * dx=dy=dz=buttons=0 before we add the event
		         * to the queue.  In any other case we delete
		         * the timeout event.
		         */
			if ((sc->sc_info[0].sc_flags & UMS_FLAG_SBU) &&
			    (dx == 0) && (dy == 0) && (dz == 0) && (dt == 0) &&
			    (dw == 0) && (buttons == 0)) {

				usb_callout_reset(&sc->sc_callout, hz / 20,
				    &ums_put_queue_timeout, sc);
			} else {

				usb_callout_stop(&sc->sc_callout);

				ums_put_queue(sc, dx, dy, dz, dt, buttons);
#ifdef EVDEV_SUPPORT
				ums_evdev_push(sc, dx, dy, dz, dt,
				    buttons_reported);
#endif

			}
		}
	case USB_ST_SETUP:
tr_setup:
		/* check if we can put more data into the FIFO */
		if (usb_fifo_put_bytes_max(sc->sc_fifo.fp[USB_FIFO_RX]) == 0) {
#ifdef EVDEV_SUPPORT
			if (sc->sc_evflags == 0)
				break;
#else
			break;
#endif
		}

		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static const struct usb_config ums_config[UMS_N_TRANSFER] = {

	[UMS_INTR_DT] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,	/* use wMaxPacketSize */
		.callback = &ums_intr_callback,
	},
};

/* A match on these entries will load ums */
static const STRUCT_USB_HOST_ID __used ums_devs[] = {
	{USB_IFACE_CLASS(UICLASS_HID),
	 USB_IFACE_SUBCLASS(UISUBCLASS_BOOT),
	 USB_IFACE_PROTOCOL(UIPROTO_MOUSE),},
};

static int
ums_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	void *d_ptr;
	int error;
	uint16_t d_len;

	DPRINTFN(11, "\n");

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);

	if (uaa->info.bInterfaceClass != UICLASS_HID)
		return (ENXIO);

	if (usb_test_quirk(uaa, UQ_UMS_IGNORE))
		return (ENXIO);

	if ((uaa->info.bInterfaceSubClass == UISUBCLASS_BOOT) &&
	    (uaa->info.bInterfaceProtocol == UIPROTO_MOUSE))
		return (BUS_PROBE_DEFAULT);

	error = usbd_req_get_hid_desc(uaa->device, NULL,
	    &d_ptr, &d_len, M_TEMP, uaa->info.bIfaceIndex);

	if (error)
		return (ENXIO);

	if (hid_is_mouse(d_ptr, d_len))
		error = BUS_PROBE_DEFAULT;
	else
		error = ENXIO;

	free(d_ptr, M_TEMP);
	return (error);
}

static void
ums_hid_parse(struct ums_softc *sc, device_t dev, const uint8_t *buf,
    uint16_t len, uint8_t index)
{
	struct ums_info *info = &sc->sc_info[index];
	uint32_t flags;
	uint8_t i;
	uint8_t j;

	if (hid_locate(buf, len, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X),
	    hid_input, index, &info->sc_loc_x, &flags, &info->sc_iid_x)) {

		if ((flags & MOUSE_FLAGS_MASK) == MOUSE_FLAGS) {
			info->sc_flags |= UMS_FLAG_X_AXIS;
		}
	}
	if (hid_locate(buf, len, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y),
	    hid_input, index, &info->sc_loc_y, &flags, &info->sc_iid_y)) {

		if ((flags & MOUSE_FLAGS_MASK) == MOUSE_FLAGS) {
			info->sc_flags |= UMS_FLAG_Y_AXIS;
		}
	}
	/* Try the wheel first as the Z activator since it's tradition. */
	if (hid_locate(buf, len, HID_USAGE2(HUP_GENERIC_DESKTOP,
	    HUG_WHEEL), hid_input, index, &info->sc_loc_z, &flags,
	    &info->sc_iid_z) ||
	    hid_locate(buf, len, HID_USAGE2(HUP_GENERIC_DESKTOP,
	    HUG_TWHEEL), hid_input, index, &info->sc_loc_z, &flags,
	    &info->sc_iid_z)) {
		if ((flags & MOUSE_FLAGS_MASK) == MOUSE_FLAGS) {
			info->sc_flags |= UMS_FLAG_Z_AXIS;
		}
		/*
		 * We might have both a wheel and Z direction, if so put
		 * put the Z on the W coordinate.
		 */
		if (hid_locate(buf, len, HID_USAGE2(HUP_GENERIC_DESKTOP,
		    HUG_Z), hid_input, index, &info->sc_loc_w, &flags,
		    &info->sc_iid_w)) {

			if ((flags & MOUSE_FLAGS_MASK) == MOUSE_FLAGS) {
				info->sc_flags |= UMS_FLAG_W_AXIS;
			}
		}
	} else if (hid_locate(buf, len, HID_USAGE2(HUP_GENERIC_DESKTOP,
	    HUG_Z), hid_input, index, &info->sc_loc_z, &flags, 
	    &info->sc_iid_z)) {

		if ((flags & MOUSE_FLAGS_MASK) == MOUSE_FLAGS) {
			info->sc_flags |= UMS_FLAG_Z_AXIS;
		}
	}
	/*
	 * The Microsoft Wireless Intellimouse 2.0 reports it's wheel
	 * using 0x0048, which is HUG_TWHEEL, and seems to expect you
	 * to know that the byte after the wheel is the tilt axis.
	 * There are no other HID axis descriptors other than X,Y and
	 * TWHEEL
	 */
	if (hid_locate(buf, len, HID_USAGE2(HUP_GENERIC_DESKTOP,
	    HUG_TWHEEL), hid_input, index, &info->sc_loc_t, 
	    &flags, &info->sc_iid_t)) {

		info->sc_loc_t.pos += 8;

		if ((flags & MOUSE_FLAGS_MASK) == MOUSE_FLAGS) {
			info->sc_flags |= UMS_FLAG_T_AXIS;
		}
	} else if (hid_locate(buf, len, HID_USAGE2(HUP_CONSUMER,
		HUC_AC_PAN), hid_input, index, &info->sc_loc_t,
		&flags, &info->sc_iid_t)) {

		if ((flags & MOUSE_FLAGS_MASK) == MOUSE_FLAGS)
			info->sc_flags |= UMS_FLAG_T_AXIS;
	}
	/* figure out the number of buttons */

	for (i = 0; i < UMS_BUTTON_MAX; i++) {
		if (!hid_locate(buf, len, HID_USAGE2(HUP_BUTTON, (i + 1)),
		    hid_input, index, &info->sc_loc_btn[i], NULL, 
		    &info->sc_iid_btn[i])) {
			break;
		}
	}

	/* detect other buttons */

	for (j = 0; (i < UMS_BUTTON_MAX) && (j < 2); i++, j++) {
		if (!hid_locate(buf, len, HID_USAGE2(HUP_MICROSOFT, (j + 1)),
		    hid_input, index, &info->sc_loc_btn[i], NULL, 
		    &info->sc_iid_btn[i])) {
			break;
		}
	}

	info->sc_buttons = i;

	if (i > sc->sc_buttons)
		sc->sc_buttons = i;

	if (info->sc_flags == 0)
		return;

	/* announce information about the mouse */
	device_printf(dev, "%d buttons and [%s%s%s%s%s] coordinates ID=%u\n",
	    (info->sc_buttons),
	    (info->sc_flags & UMS_FLAG_X_AXIS) ? "X" : "",
	    (info->sc_flags & UMS_FLAG_Y_AXIS) ? "Y" : "",
	    (info->sc_flags & UMS_FLAG_Z_AXIS) ? "Z" : "",
	    (info->sc_flags & UMS_FLAG_T_AXIS) ? "T" : "",
	    (info->sc_flags & UMS_FLAG_W_AXIS) ? "W" : "",
	    info->sc_iid_x);
}

static int
ums_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct ums_softc *sc = device_get_softc(dev);
	struct ums_info *info;
	void *d_ptr = NULL;
	int isize;
	int err;
	uint16_t d_len;
	uint8_t i;
#ifdef USB_DEBUG
	uint8_t j;
#endif

	DPRINTFN(11, "sc=%p\n", sc);

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, "ums lock", NULL, MTX_DEF | MTX_RECURSE);

	usb_callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);

	/*
         * Force the report (non-boot) protocol.
         *
         * Mice without boot protocol support may choose not to implement
         * Set_Protocol at all; Ignore any error.
         */
	err = usbd_req_set_protocol(uaa->device, NULL,
	    uaa->info.bIfaceIndex, 1);

	err = usbd_transfer_setup(uaa->device,
	    &uaa->info.bIfaceIndex, sc->sc_xfer, ums_config,
	    UMS_N_TRANSFER, sc, &sc->sc_mtx);

	if (err) {
		DPRINTF("error=%s\n", usbd_errstr(err));
		goto detach;
	}

	/* Get HID descriptor */
	err = usbd_req_get_hid_desc(uaa->device, NULL, &d_ptr,
	    &d_len, M_TEMP, uaa->info.bIfaceIndex);

	if (err) {
		device_printf(dev, "error reading report description\n");
		goto detach;
	}

	isize = hid_report_size(d_ptr, d_len, hid_input, &sc->sc_iid);

	/*
	 * The Microsoft Wireless Notebook Optical Mouse seems to be in worse
	 * shape than the Wireless Intellimouse 2.0, as its X, Y, wheel, and
	 * all of its other button positions are all off. It also reports that
	 * it has two additional buttons and a tilt wheel.
	 */
	if (usb_test_quirk(uaa, UQ_MS_BAD_CLASS)) {

		sc->sc_iid = 0;

		info = &sc->sc_info[0];
		info->sc_flags = (UMS_FLAG_X_AXIS |
		    UMS_FLAG_Y_AXIS |
		    UMS_FLAG_Z_AXIS |
		    UMS_FLAG_SBU);
		info->sc_buttons = 3;
		isize = 5;
		/* 1st byte of descriptor report contains garbage */
		info->sc_loc_x.pos = 16;
		info->sc_loc_x.size = 8;
		info->sc_loc_y.pos = 24;
		info->sc_loc_y.size = 8;
		info->sc_loc_z.pos = 32;
		info->sc_loc_z.size = 8;
		info->sc_loc_btn[0].pos = 8;
		info->sc_loc_btn[0].size = 1;
		info->sc_loc_btn[1].pos = 9;
		info->sc_loc_btn[1].size = 1;
		info->sc_loc_btn[2].pos = 10;
		info->sc_loc_btn[2].size = 1;

		/* Announce device */
		device_printf(dev, "3 buttons and [XYZ] "
		    "coordinates ID=0\n");

	} else {
		/* Search the HID descriptor and announce device */
		for (i = 0; i < UMS_INFO_MAX; i++) {
			ums_hid_parse(sc, dev, d_ptr, d_len, i);
		}
	}

	if (usb_test_quirk(uaa, UQ_MS_REVZ)) {
		info = &sc->sc_info[0];
		/* Some wheels need the Z axis reversed. */
		info->sc_flags |= UMS_FLAG_REVZ;
	}
	if (isize > (int)usbd_xfer_max_framelen(sc->sc_xfer[UMS_INTR_DT])) {
		DPRINTF("WARNING: report size, %d bytes, is larger "
		    "than interrupt size, %d bytes!\n", isize,
		    usbd_xfer_max_framelen(sc->sc_xfer[UMS_INTR_DT]));
	}
	free(d_ptr, M_TEMP);
	d_ptr = NULL;

#ifdef USB_DEBUG
	for (j = 0; j < UMS_INFO_MAX; j++) {
		info = &sc->sc_info[j];

		DPRINTF("sc=%p, index=%d\n", sc, j);
		DPRINTF("X\t%d/%d id=%d\n", info->sc_loc_x.pos,
		    info->sc_loc_x.size, info->sc_iid_x);
		DPRINTF("Y\t%d/%d id=%d\n", info->sc_loc_y.pos,
		    info->sc_loc_y.size, info->sc_iid_y);
		DPRINTF("Z\t%d/%d id=%d\n", info->sc_loc_z.pos,
		    info->sc_loc_z.size, info->sc_iid_z);
		DPRINTF("T\t%d/%d id=%d\n", info->sc_loc_t.pos,
		    info->sc_loc_t.size, info->sc_iid_t);
		DPRINTF("W\t%d/%d id=%d\n", info->sc_loc_w.pos,
		    info->sc_loc_w.size, info->sc_iid_w);

		for (i = 0; i < info->sc_buttons; i++) {
			DPRINTF("B%d\t%d/%d id=%d\n",
			    i + 1, info->sc_loc_btn[i].pos,
			    info->sc_loc_btn[i].size, info->sc_iid_btn[i]);
		}
	}
	DPRINTF("size=%d, id=%d\n", isize, sc->sc_iid);
#endif

	err = usb_fifo_attach(uaa->device, sc, &sc->sc_mtx,
	    &ums_fifo_methods, &sc->sc_fifo,
	    device_get_unit(dev), -1, uaa->info.bIfaceIndex,
  	    UID_ROOT, GID_OPERATOR, 0644);
	if (err)
		goto detach;

#ifdef EVDEV_SUPPORT
	sc->sc_evdev = evdev_alloc();
	evdev_set_name(sc->sc_evdev, device_get_desc(dev));
	evdev_set_phys(sc->sc_evdev, device_get_nameunit(dev));
	evdev_set_id(sc->sc_evdev, BUS_USB, uaa->info.idVendor,
	    uaa->info.idProduct, 0);
	evdev_set_serial(sc->sc_evdev, usb_get_serial(uaa->device));
	evdev_set_methods(sc->sc_evdev, sc, &ums_evdev_methods);
	evdev_support_prop(sc->sc_evdev, INPUT_PROP_POINTER);
	evdev_support_event(sc->sc_evdev, EV_SYN);
	evdev_support_event(sc->sc_evdev, EV_REL);
	evdev_support_event(sc->sc_evdev, EV_KEY);

	info = &sc->sc_info[0];

	if (info->sc_flags & UMS_FLAG_X_AXIS)
		evdev_support_rel(sc->sc_evdev, REL_X);

	if (info->sc_flags & UMS_FLAG_Y_AXIS)
		evdev_support_rel(sc->sc_evdev, REL_Y);

	if (info->sc_flags & UMS_FLAG_Z_AXIS)
		evdev_support_rel(sc->sc_evdev, REL_WHEEL);

	if (info->sc_flags & UMS_FLAG_T_AXIS)
		evdev_support_rel(sc->sc_evdev, REL_HWHEEL);

	for (i = 0; i < info->sc_buttons; i++)
		evdev_support_key(sc->sc_evdev, BTN_MOUSE + i);

	err = evdev_register_mtx(sc->sc_evdev, &sc->sc_mtx);
	if (err)
		goto detach;
#endif

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "parseinfo", CTLTYPE_STRING|CTLFLAG_RD,
	    sc, 0, ums_sysctl_handler_parseinfo,
	    "", "Dump of parsed HID report descriptor");

	return (0);

detach:
	if (d_ptr) {
		free(d_ptr, M_TEMP);
	}
	ums_detach(dev);
	return (ENOMEM);
}

static int
ums_detach(device_t self)
{
	struct ums_softc *sc = device_get_softc(self);

	DPRINTF("sc=%p\n", sc);

	usb_fifo_detach(&sc->sc_fifo);

#ifdef EVDEV_SUPPORT
	evdev_free(sc->sc_evdev);
#endif

	usbd_transfer_unsetup(sc->sc_xfer, UMS_N_TRANSFER);

	usb_callout_drain(&sc->sc_callout);

	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static void
ums_reset(struct ums_softc *sc)
{

	/* reset all USB mouse parameters */

	if (sc->sc_buttons > MOUSE_MSC_MAXBUTTON)
		sc->sc_hw.buttons = MOUSE_MSC_MAXBUTTON;
	else
		sc->sc_hw.buttons = sc->sc_buttons;

	sc->sc_hw.iftype = MOUSE_IF_USB;
	sc->sc_hw.type = MOUSE_MOUSE;
	sc->sc_hw.model = MOUSE_MODEL_GENERIC;
	sc->sc_hw.hwid = 0;

	sc->sc_mode.protocol = MOUSE_PROTO_MSC;
	sc->sc_mode.rate = -1;
	sc->sc_mode.resolution = MOUSE_RES_UNKNOWN;
	sc->sc_mode.accelfactor = 0;
	sc->sc_mode.level = 0;
	sc->sc_mode.packetsize = MOUSE_MSC_PACKETSIZE;
	sc->sc_mode.syncmask[0] = MOUSE_MSC_SYNCMASK;
	sc->sc_mode.syncmask[1] = MOUSE_MSC_SYNC;

	/* reset status */

	sc->sc_status.flags = 0;
	sc->sc_status.button = 0;
	sc->sc_status.obutton = 0;
	sc->sc_status.dx = 0;
	sc->sc_status.dy = 0;
	sc->sc_status.dz = 0;
	/* sc->sc_status.dt = 0; */
}

static void
ums_start_rx(struct ums_softc *sc)
{
	int rate;

	/* Check if we should override the default polling interval */
	rate = sc->sc_pollrate;
	/* Range check rate */
	if (rate > 1000)
		rate = 1000;
	/* Check for set rate */
	if ((rate > 0) && (sc->sc_xfer[UMS_INTR_DT] != NULL)) {
		DPRINTF("Setting pollrate = %d\n", rate);
		/* Stop current transfer, if any */
		usbd_transfer_stop(sc->sc_xfer[UMS_INTR_DT]);
		/* Set new interval */
		usbd_xfer_set_interval(sc->sc_xfer[UMS_INTR_DT], 1000 / rate);
		/* Only set pollrate once */
		sc->sc_pollrate = 0;
	}

	usbd_transfer_start(sc->sc_xfer[UMS_INTR_DT]);
}

static void
ums_stop_rx(struct ums_softc *sc)
{
	usbd_transfer_stop(sc->sc_xfer[UMS_INTR_DT]);
	usb_callout_stop(&sc->sc_callout);
}

static void
ums_fifo_start_read(struct usb_fifo *fifo)
{
	struct ums_softc *sc = usb_fifo_softc(fifo);

	ums_start_rx(sc);
}

static void
ums_fifo_stop_read(struct usb_fifo *fifo)
{
	struct ums_softc *sc = usb_fifo_softc(fifo);

	ums_stop_rx(sc);
}


#if ((MOUSE_SYS_PACKETSIZE != 8) || \
     (MOUSE_MSC_PACKETSIZE != 5))
#error "Software assumptions are not met. Please update code."
#endif

static void
ums_put_queue(struct ums_softc *sc, int32_t dx, int32_t dy,
    int32_t dz, int32_t dt, int32_t buttons)
{
	uint8_t buf[8];

	if (1) {

		if (dx > 254)
			dx = 254;
		if (dx < -256)
			dx = -256;
		if (dy > 254)
			dy = 254;
		if (dy < -256)
			dy = -256;
		if (dz > 126)
			dz = 126;
		if (dz < -128)
			dz = -128;
		if (dt > 126)
			dt = 126;
		if (dt < -128)
			dt = -128;

		buf[0] = sc->sc_mode.syncmask[1];
		buf[0] |= (~buttons) & MOUSE_MSC_BUTTONS;
		buf[1] = dx >> 1;
		buf[2] = dy >> 1;
		buf[3] = dx - (dx >> 1);
		buf[4] = dy - (dy >> 1);

		if (sc->sc_mode.level == 1) {
			buf[5] = dz >> 1;
			buf[6] = dz - (dz >> 1);
			buf[7] = (((~buttons) >> 3) & MOUSE_SYS_EXTBUTTONS);
		}
		usb_fifo_put_data_linear(sc->sc_fifo.fp[USB_FIFO_RX], buf,
		    sc->sc_mode.packetsize, 1);
	} else {
		DPRINTF("Buffer full, discarded packet\n");
	}
}

#ifdef EVDEV_SUPPORT
static void
ums_evdev_push(struct ums_softc *sc, int32_t dx, int32_t dy,
    int32_t dz, int32_t dt, int32_t buttons)
{
	if (evdev_rcpt_mask & EVDEV_RCPT_HW_MOUSE) {
		/* Push evdev event */
		evdev_push_rel(sc->sc_evdev, REL_X, dx);
		evdev_push_rel(sc->sc_evdev, REL_Y, -dy);
		evdev_push_rel(sc->sc_evdev, REL_WHEEL, -dz);
		evdev_push_rel(sc->sc_evdev, REL_HWHEEL, dt);
		evdev_push_mouse_btn(sc->sc_evdev,
		    (buttons & ~MOUSE_STDBUTTONS) |
		    (buttons & (1 << 2) ? MOUSE_BUTTON1DOWN : 0) |
		    (buttons & (1 << 1) ? MOUSE_BUTTON2DOWN : 0) |
		    (buttons & (1 << 0) ? MOUSE_BUTTON3DOWN : 0));
		evdev_sync(sc->sc_evdev);
	}
}
#endif

static void
ums_reset_buf(struct ums_softc *sc)
{
	/* reset read queue, must be called locked */
	usb_fifo_reset(sc->sc_fifo.fp[USB_FIFO_RX]);
}

#ifdef EVDEV_SUPPORT
static int
ums_ev_open(struct evdev_dev *evdev)
{
	struct ums_softc *sc = evdev_get_softc(evdev);

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	sc->sc_evflags = UMS_EVDEV_OPENED;

	if (sc->sc_fflags == 0) {
		ums_reset(sc);
		ums_start_rx(sc);
	}

	return (0);
}

static int
ums_ev_close(struct evdev_dev *evdev)
{
	struct ums_softc *sc = evdev_get_softc(evdev);

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	sc->sc_evflags = 0;

	if (sc->sc_fflags == 0)
		ums_stop_rx(sc);

	return (0);
}
#endif

static int
ums_fifo_open(struct usb_fifo *fifo, int fflags)
{
	struct ums_softc *sc = usb_fifo_softc(fifo);

	DPRINTFN(2, "\n");

	/* check for duplicate open, should not happen */
	if (sc->sc_fflags & fflags)
		return (EBUSY);

	/* check for first open */
#ifdef EVDEV_SUPPORT
	if (sc->sc_fflags == 0 && sc->sc_evflags == 0)
		ums_reset(sc);
#else
	if (sc->sc_fflags == 0)
		ums_reset(sc);
#endif

	if (fflags & FREAD) {
		/* allocate RX buffer */
		if (usb_fifo_alloc_buffer(fifo,
		    UMS_BUF_SIZE, UMS_IFQ_MAXLEN)) {
			return (ENOMEM);
		}
	}

	sc->sc_fflags |= fflags & (FREAD | FWRITE);
	return (0);
}

static void
ums_fifo_close(struct usb_fifo *fifo, int fflags)
{
	struct ums_softc *sc = usb_fifo_softc(fifo);

	DPRINTFN(2, "\n");

	if (fflags & FREAD)
		usb_fifo_free_buffer(fifo);

	sc->sc_fflags &= ~(fflags & (FREAD | FWRITE));
}

static int
ums_fifo_ioctl(struct usb_fifo *fifo, u_long cmd, void *addr, int fflags)
{
	struct ums_softc *sc = usb_fifo_softc(fifo);
	mousemode_t mode;
	int error = 0;

	DPRINTFN(2, "\n");

	mtx_lock(&sc->sc_mtx);

	switch (cmd) {
	case MOUSE_GETHWINFO:
		*(mousehw_t *)addr = sc->sc_hw;
		break;

	case MOUSE_GETMODE:
		*(mousemode_t *)addr = sc->sc_mode;
		break;

	case MOUSE_SETMODE:
		mode = *(mousemode_t *)addr;

		if (mode.level == -1) {
			/* don't change the current setting */
		} else if ((mode.level < 0) || (mode.level > 1)) {
			error = EINVAL;
			break;
		} else {
			sc->sc_mode.level = mode.level;
		}

		/* store polling rate */
		sc->sc_pollrate = mode.rate;

		if (sc->sc_mode.level == 0) {
			if (sc->sc_buttons > MOUSE_MSC_MAXBUTTON)
				sc->sc_hw.buttons = MOUSE_MSC_MAXBUTTON;
			else
				sc->sc_hw.buttons = sc->sc_buttons;
			sc->sc_mode.protocol = MOUSE_PROTO_MSC;
			sc->sc_mode.packetsize = MOUSE_MSC_PACKETSIZE;
			sc->sc_mode.syncmask[0] = MOUSE_MSC_SYNCMASK;
			sc->sc_mode.syncmask[1] = MOUSE_MSC_SYNC;
		} else if (sc->sc_mode.level == 1) {
			if (sc->sc_buttons > MOUSE_SYS_MAXBUTTON)
				sc->sc_hw.buttons = MOUSE_SYS_MAXBUTTON;
			else
				sc->sc_hw.buttons = sc->sc_buttons;
			sc->sc_mode.protocol = MOUSE_PROTO_SYSMOUSE;
			sc->sc_mode.packetsize = MOUSE_SYS_PACKETSIZE;
			sc->sc_mode.syncmask[0] = MOUSE_SYS_SYNCMASK;
			sc->sc_mode.syncmask[1] = MOUSE_SYS_SYNC;
		}
		ums_reset_buf(sc);
		break;

	case MOUSE_GETLEVEL:
		*(int *)addr = sc->sc_mode.level;
		break;

	case MOUSE_SETLEVEL:
		if (*(int *)addr < 0 || *(int *)addr > 1) {
			error = EINVAL;
			break;
		}
		sc->sc_mode.level = *(int *)addr;

		if (sc->sc_mode.level == 0) {
			if (sc->sc_buttons > MOUSE_MSC_MAXBUTTON)
				sc->sc_hw.buttons = MOUSE_MSC_MAXBUTTON;
			else
				sc->sc_hw.buttons = sc->sc_buttons;
			sc->sc_mode.protocol = MOUSE_PROTO_MSC;
			sc->sc_mode.packetsize = MOUSE_MSC_PACKETSIZE;
			sc->sc_mode.syncmask[0] = MOUSE_MSC_SYNCMASK;
			sc->sc_mode.syncmask[1] = MOUSE_MSC_SYNC;
		} else if (sc->sc_mode.level == 1) {
			if (sc->sc_buttons > MOUSE_SYS_MAXBUTTON)
				sc->sc_hw.buttons = MOUSE_SYS_MAXBUTTON;
			else
				sc->sc_hw.buttons = sc->sc_buttons;
			sc->sc_mode.protocol = MOUSE_PROTO_SYSMOUSE;
			sc->sc_mode.packetsize = MOUSE_SYS_PACKETSIZE;
			sc->sc_mode.syncmask[0] = MOUSE_SYS_SYNCMASK;
			sc->sc_mode.syncmask[1] = MOUSE_SYS_SYNC;
		}
		ums_reset_buf(sc);
		break;

	case MOUSE_GETSTATUS:{
			mousestatus_t *status = (mousestatus_t *)addr;

			*status = sc->sc_status;
			sc->sc_status.obutton = sc->sc_status.button;
			sc->sc_status.button = 0;
			sc->sc_status.dx = 0;
			sc->sc_status.dy = 0;
			sc->sc_status.dz = 0;
			/* sc->sc_status.dt = 0; */

			if (status->dx || status->dy || status->dz /* || status->dt */ ) {
				status->flags |= MOUSE_POSCHANGED;
			}
			if (status->button != status->obutton) {
				status->flags |= MOUSE_BUTTONSCHANGED;
			}
			break;
		}
	default:
		error = ENOTTY;
		break;
	}

	mtx_unlock(&sc->sc_mtx);
	return (error);
}

static int
ums_sysctl_handler_parseinfo(SYSCTL_HANDLER_ARGS)
{
	struct ums_softc *sc = arg1;
	struct ums_info *info;
	struct sbuf *sb;
	int i, j, err, had_output;

	sb = sbuf_new_auto();
	for (i = 0, had_output = 0; i < UMS_INFO_MAX; i++) {
		info = &sc->sc_info[i];

		/* Don't emit empty info */
		if ((info->sc_flags &
		    (UMS_FLAG_X_AXIS | UMS_FLAG_Y_AXIS | UMS_FLAG_Z_AXIS |
		     UMS_FLAG_T_AXIS | UMS_FLAG_W_AXIS)) == 0 &&
		    info->sc_buttons == 0)
			continue;

		if (had_output)
			sbuf_printf(sb, "\n");
		had_output = 1;
		sbuf_printf(sb, "i%d:", i + 1);
		if (info->sc_flags & UMS_FLAG_X_AXIS)
			sbuf_printf(sb, " X:r%d, p%d, s%d;",
			    (int)info->sc_iid_x,
			    (int)info->sc_loc_x.pos,
			    (int)info->sc_loc_x.size);
		if (info->sc_flags & UMS_FLAG_Y_AXIS)
			sbuf_printf(sb, " Y:r%d, p%d, s%d;",
			    (int)info->sc_iid_y,
			    (int)info->sc_loc_y.pos,
			    (int)info->sc_loc_y.size);
		if (info->sc_flags & UMS_FLAG_Z_AXIS)
			sbuf_printf(sb, " Z:r%d, p%d, s%d;",
			    (int)info->sc_iid_z,
			    (int)info->sc_loc_z.pos,
			    (int)info->sc_loc_z.size);
		if (info->sc_flags & UMS_FLAG_T_AXIS)
			sbuf_printf(sb, " T:r%d, p%d, s%d;",
			    (int)info->sc_iid_t,
			    (int)info->sc_loc_t.pos,
			    (int)info->sc_loc_t.size);
		if (info->sc_flags & UMS_FLAG_W_AXIS)
			sbuf_printf(sb, " W:r%d, p%d, s%d;",
			    (int)info->sc_iid_w,
			    (int)info->sc_loc_w.pos,
			    (int)info->sc_loc_w.size);

		for (j = 0; j < info->sc_buttons; j++) {
			sbuf_printf(sb, " B%d:r%d, p%d, s%d;", j + 1,
			    (int)info->sc_iid_btn[j],
			    (int)info->sc_loc_btn[j].pos,
			    (int)info->sc_loc_btn[j].size);
		}
	}
	sbuf_finish(sb);
	err = SYSCTL_OUT(req, sbuf_data(sb), sbuf_len(sb) + 1);
	sbuf_delete(sb);

	return (err);
}

static devclass_t ums_devclass;

static device_method_t ums_methods[] = {
	DEVMETHOD(device_probe, ums_probe),
	DEVMETHOD(device_attach, ums_attach),
	DEVMETHOD(device_detach, ums_detach),

	DEVMETHOD_END
};

static driver_t ums_driver = {
	.name = "ums",
	.methods = ums_methods,
	.size = sizeof(struct ums_softc),
};

DRIVER_MODULE(ums, uhub, ums_driver, ums_devclass, NULL, 0);
MODULE_DEPEND(ums, usb, 1, 1, 1);
#ifdef EVDEV_SUPPORT
MODULE_DEPEND(ums, evdev, 1, 1, 1);
#endif
MODULE_VERSION(ums, 1);
USB_PNP_HOST_INFO(ums_devs);
