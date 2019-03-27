/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2013, Michael Terrell <vashisnotatree@gmail.com>
 * Copyright 2018, Johannes Lundberg <johalun0@gmail.com>
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/syslog.h>
#include <sys/fcntl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>

#include <dev/usb/usbhid.h>
#include <dev/usb/usb_ioctl.h>

#include "usb_rdesc.h"

#define	UHID_SNES_IFQ_MAX_LEN 8

#define	UREQ_GET_PORT_STATUS 0x01
#define	UREQ_SOFT_RESET      0x02

#define	UP      0x7f00
#define	DOWN    0x7fff
#define	LEFT    0x00ff
#define	RIGHT   0xff7f
#define	X       0x1f
#define	Y       0x8f
#define	A       0x2f
#define	B       0x4f
#define	SELECT  0x10
#define	START   0x20
#define	LEFT_T  0x01
#define	RIGHT_T 0x02

static const uint8_t uhid_snes_report_descr[] = { UHID_SNES_REPORT_DESCR() };

#define	SNES_DEV(v,p,i) { USB_VPI(v,p,i) }

static const STRUCT_USB_HOST_ID snes_devs[] = {
	SNES_DEV(0x0810, 0xe501, 0), /* GeeekPi K-0161 */
	SNES_DEV(0x0079, 0x0011, 0)  /* Dragonrise */
};

enum {
	UHID_SNES_INTR_DT_RD,
	UHID_SNES_STATUS_DT_RD,
	UHID_SNES_N_TRANSFER
};

struct uhid_snes_softc {
	device_t sc_dev;
	struct usb_device *sc_usb_device;
	struct mtx sc_mutex;
	struct usb_callout sc_watchdog;
	uint8_t sc_iface_num;
	struct usb_xfer *sc_transfer[UHID_SNES_N_TRANSFER];
	struct usb_fifo_sc sc_fifo;
	struct usb_fifo_sc sc_fifo_no_reset;
	int sc_fflags;
	struct usb_fifo *sc_fifo_open[2];
	uint8_t sc_zero_length_packets;
	uint8_t sc_previous_status;
	uint8_t sc_iid;
	uint8_t sc_oid;
	uint8_t sc_fid;
	uint8_t sc_iface_index;

	uint32_t sc_isize;
	uint32_t sc_osize;
	uint32_t sc_fsize;

	void *sc_repdesc_ptr;

	uint16_t sc_repdesc_size;

	struct usb_device *sc_udev;
#define	UHID_FLAG_IMMED        0x01	/* set if read should be immediate */

};

static device_probe_t uhid_snes_probe;
static device_attach_t uhid_snes_attach;
static device_detach_t uhid_snes_detach;

static usb_fifo_open_t uhid_snes_open;
static usb_fifo_close_t uhid_snes_close;
static usb_fifo_ioctl_t uhid_snes_ioctl;
static usb_fifo_cmd_t uhid_snes_start_read;
static usb_fifo_cmd_t uhid_snes_stop_read;

static void uhid_snes_reset(struct uhid_snes_softc *);
static void uhid_snes_watchdog(void *);

static usb_callback_t uhid_snes_read_callback;
static usb_callback_t uhid_snes_status_callback;

static struct usb_fifo_methods uhid_snes_fifo_methods = {
	.f_open = &uhid_snes_open,
	.f_close = &uhid_snes_close,
	.f_ioctl = &uhid_snes_ioctl,
	.f_start_read = &uhid_snes_start_read,
	.f_stop_read = &uhid_snes_stop_read,
	.basename[0] = "uhid_snes"
};

static const struct usb_config uhid_snes_config[UHID_SNES_N_TRANSFER] = {
	[UHID_SNES_INTR_DT_RD] = {
		.callback = &uhid_snes_read_callback,
		.bufsize = sizeof(struct usb_device_request) +1,
		.flags = {.short_xfer_ok = 1, .short_frames_ok = 1,
			  .pipe_bof =1, .proxy_buffer =1},
		.type = UE_INTERRUPT,
		.endpoint = 0x81,
		.direction = UE_DIR_IN
	},
	[UHID_SNES_STATUS_DT_RD] = {
		.callback = &uhid_snes_status_callback,
		.bufsize = sizeof(struct usb_device_request) + 1,
		.timeout = 1000,
		.type = UE_CONTROL,
		.endpoint = 0x00,
		.direction = UE_DIR_ANY
	}
};

static int
uhid_get_report(struct uhid_snes_softc *sc, uint8_t type,
    uint8_t id, void *kern_data, void *user_data, uint16_t len)
{
	int err;
	uint8_t free_data = 0;

	if (kern_data == NULL) {
		kern_data = malloc(len, M_USBDEV, M_WAITOK);
		if (kern_data == NULL) {
			err = ENOMEM;
			goto done;
		}
		free_data = 1;
	}
	err = usbd_req_get_report(sc->sc_udev, NULL, kern_data,
	    len, sc->sc_iface_index, type, id);
	if (err) {
		err = ENXIO;
		goto done;
	}
	if (user_data) {
		/* dummy buffer */
		err = copyout(kern_data, user_data, len);
		if (err) {
			goto done;
		}
	}
done:
	if (free_data) {
		free(kern_data, M_USBDEV);
	}
	return (err);
}

static int
uhid_set_report(struct uhid_snes_softc *sc, uint8_t type,
    uint8_t id, void *kern_data, void *user_data, uint16_t len)
{
	int err;
	uint8_t free_data = 0;

	if (kern_data == NULL) {
		kern_data = malloc(len, M_USBDEV, M_WAITOK);
		if (kern_data == NULL) {
			err = ENOMEM;
			goto done;
		}
		free_data = 1;
		err = copyin(user_data, kern_data, len);
		if (err) {
			goto done;
		}
	}
	err = usbd_req_set_report(sc->sc_udev, NULL, kern_data,
	    len, sc->sc_iface_index, type, id);
	if (err) {
		err = ENXIO;
		goto done;
	}
done:
	if (free_data) {
		free(kern_data, M_USBDEV);
	}
	return (err);
}

static int
uhid_snes_open(struct usb_fifo *fifo, int fflags)
{
	struct uhid_snes_softc *sc = usb_fifo_softc(fifo);
	int error;

	if (sc->sc_fflags & fflags) {
		uhid_snes_reset(sc);
		return (EBUSY);
	}

	mtx_lock(&sc->sc_mutex);
	usbd_xfer_set_stall(sc->sc_transfer[UHID_SNES_INTR_DT_RD]);
	mtx_unlock(&sc->sc_mutex);

	error = usb_fifo_alloc_buffer(fifo,
	    usbd_xfer_max_len(sc->sc_transfer[UHID_SNES_INTR_DT_RD]),
	    UHID_SNES_IFQ_MAX_LEN);
	if (error)
		return (ENOMEM);

	sc->sc_fifo_open[USB_FIFO_RX] = fifo;

	return (0);
}

static void
uhid_snes_reset(struct uhid_snes_softc *sc)
{
	struct usb_device_request req;
	int error;

	req.bRequest = UREQ_SOFT_RESET;
	USETW(req.wValue, 0);
	USETW(req.wIndex, sc->sc_iface_num);
	USETW(req.wLength, 0);

	mtx_lock(&sc->sc_mutex);

	error = usbd_do_request_flags(sc->sc_usb_device, &sc->sc_mutex,
	    &req, NULL, 0, NULL, 2 * USB_MS_HZ);

	if (error) {
		usbd_do_request_flags(sc->sc_usb_device, &sc->sc_mutex,
		    &req, NULL, 0, NULL, 2 * USB_MS_HZ);
	}

	mtx_unlock(&sc->sc_mutex);
}

static void
uhid_snes_close(struct usb_fifo *fifo, int fflags)
{
	struct uhid_snes_softc *sc = usb_fifo_softc(fifo);

	sc->sc_fflags &= ~(fflags & FREAD);
	usb_fifo_free_buffer(fifo);
}

static int
uhid_snes_ioctl(struct usb_fifo *fifo, u_long cmd, void *data, int fflags)
{
	struct uhid_snes_softc *sc = usb_fifo_softc(fifo);
	struct usb_gen_descriptor *ugd;
	uint32_t size;
	int error = 0;
	uint8_t id;

	switch (cmd) {
	case USB_GET_REPORT_DESC:
		ugd = data;
		if (sc->sc_repdesc_size > ugd->ugd_maxlen) {
			size = ugd->ugd_maxlen;
		} else {
			size = sc->sc_repdesc_size;
		}

		ugd->ugd_actlen = size;
		if (ugd->ugd_data == NULL)
			break; /*desciptor length only*/
		error = copyout(sc->sc_repdesc_ptr, ugd->ugd_data, size);
		break;

	case USB_SET_IMMED:
		if (!(fflags & FREAD)) {
			error = EPERM;
			break;
		}

		if (*(int *)data) {
			/* do a test read */
			error = uhid_get_report(sc, UHID_INPUT_REPORT,
			    sc->sc_iid, NULL, NULL, sc->sc_isize);
			if (error) {
				break;
			}
			mtx_lock(&sc->sc_mutex);
			sc->sc_fflags |= UHID_FLAG_IMMED;
			mtx_unlock(&sc->sc_mutex);
		} else {
			mtx_lock(&sc->sc_mutex);
			sc->sc_fflags &= ~UHID_FLAG_IMMED;
			mtx_unlock(&sc->sc_mutex);
		}
		break;

	case USB_GET_REPORT:
		if (!(fflags & FREAD)) {
			error = EPERM;
			break;
		}
		ugd = data;
		switch (ugd->ugd_report_type) {
		case UHID_INPUT_REPORT:
			size = sc->sc_isize;
			id = sc->sc_iid;
			break;
		case UHID_OUTPUT_REPORT:
			size = sc->sc_osize;
			id = sc->sc_oid;
			break;
		case UHID_FEATURE_REPORT:
			size = sc->sc_fsize;
			id = sc->sc_fid;
			break;
		default:
			return (EINVAL);
		}
		if (id != 0)
			copyin(ugd->ugd_data, &id, 1);
		error = uhid_get_report(sc, ugd->ugd_report_type, id,
		    NULL, ugd->ugd_data, imin(ugd->ugd_maxlen, size));
		break;

	case USB_SET_REPORT:
		if (!(fflags & FWRITE)) {
			error = EPERM;
			break;
		}
		ugd = data;
		switch (ugd->ugd_report_type) {
		case UHID_INPUT_REPORT:
			size = sc->sc_isize;
			id = sc->sc_iid;
			break;
		case UHID_OUTPUT_REPORT:
			size = sc->sc_osize;
			id = sc->sc_oid;
			break;
		case UHID_FEATURE_REPORT:
			size = sc->sc_fsize;
			id = sc->sc_fid;
			break;
		default:
			return (EINVAL);
		}
		if (id != 0)
			copyin(ugd->ugd_data, &id, 1);
		error = uhid_set_report(sc, ugd->ugd_report_type, id,
		    NULL, ugd->ugd_data, imin(ugd->ugd_maxlen, size));
		break;

	case USB_GET_REPORT_ID:
		/* XXX: we only support reportid 0? */
		*(int *)data = 0;
		break;

	default:
		error = EINVAL;
		break;
	}
	return (error);
}

static void
uhid_snes_watchdog(void *arg)
{
	struct uhid_snes_softc *sc = arg;

	mtx_assert(&sc->sc_mutex, MA_OWNED);

	if (sc->sc_fflags == 0)
		usbd_transfer_start(sc->sc_transfer[UHID_SNES_STATUS_DT_RD]);

	usb_callout_reset(&sc->sc_watchdog, hz, &uhid_snes_watchdog, sc);
}

static void
uhid_snes_start_read(struct usb_fifo *fifo)
{
	struct uhid_snes_softc *sc = usb_fifo_softc(fifo);

	usbd_transfer_start(sc->sc_transfer[UHID_SNES_INTR_DT_RD]);
}

static void
uhid_snes_stop_read(struct usb_fifo *fifo)
{
	struct uhid_snes_softc *sc = usb_fifo_softc(fifo);

	usbd_transfer_stop(sc->sc_transfer[UHID_SNES_INTR_DT_RD]);
}

static void
uhid_snes_read_callback(struct usb_xfer *transfer, usb_error_t error)
{
	struct uhid_snes_softc *sc = usbd_xfer_softc(transfer);
	struct usb_fifo *fifo = sc->sc_fifo_open[USB_FIFO_RX];
	struct usb_page_cache *pc;
	int actual, max;

	usbd_xfer_status(transfer, &actual, NULL, NULL, NULL);
	if (fifo == NULL)
		return;

	switch (USB_GET_STATE(transfer)) {
	case USB_ST_TRANSFERRED:
		if (actual == 0) {
			if (sc->sc_zero_length_packets == 4)
				/* Throttle transfers. */
				usbd_xfer_set_interval(transfer, 500);
			else
				sc->sc_zero_length_packets++;

		} else {
			/* disable throttling. */
			usbd_xfer_set_interval(transfer, 0);
			sc->sc_zero_length_packets = 0;
		}
		pc = usbd_xfer_get_frame(transfer, 0);
		usb_fifo_put_data(fifo, pc, 0, actual, 1);
		/* Fall through */
	setup:
	case USB_ST_SETUP:
		if (usb_fifo_put_bytes_max(fifo) != 0) {
			max = usbd_xfer_max_len(transfer);
			usbd_xfer_set_frame_len(transfer, 0, max);
			usbd_transfer_submit(transfer);
		}
		break;

	default:
		/*disable throttling. */
		usbd_xfer_set_interval(transfer, 0);
		sc->sc_zero_length_packets = 0;

		if (error != USB_ERR_CANCELLED) {
			/* Issue a clear-stall request. */
			usbd_xfer_set_stall(transfer);
			goto setup;
		}
		break;
	}
}

static void
uhid_snes_status_callback(struct usb_xfer *transfer, usb_error_t error)
{
	struct uhid_snes_softc *sc = usbd_xfer_softc(transfer);
	struct usb_device_request req;
	struct usb_page_cache *pc;
	uint8_t current_status, new_status;

	switch (USB_GET_STATE(transfer)) {
	case USB_ST_SETUP:
		req.bmRequestType = UT_READ_CLASS_INTERFACE;
		req.bRequest = UREQ_GET_PORT_STATUS;
		USETW(req.wValue, 0);
		req.wIndex[0] = sc->sc_iface_num;
		req.wIndex[1] = 0;
		USETW(req.wLength, 1);

		pc = usbd_xfer_get_frame(transfer, 0);
		usbd_copy_in(pc, 0, &req, sizeof(req));
		usbd_xfer_set_frame_len(transfer, 0, sizeof(req));
		usbd_xfer_set_frame_len(transfer, 1, 1);
		usbd_xfer_set_frames(transfer, 2);
		usbd_transfer_submit(transfer);
		break;

	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(transfer, 1);
		usbd_copy_out(pc, 0, &current_status, 1);
		new_status = current_status & ~sc->sc_previous_status;
		sc->sc_previous_status = current_status;
		break;

	default:
		break;
	}

}

static int
uhid_snes_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(snes_devs, sizeof(snes_devs), uaa));
}

static int
uhid_snes_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct uhid_snes_softc *sc = device_get_softc(dev);
	struct usb_interface_descriptor *idesc;
	struct usb_config_descriptor *cdesc;
	uint8_t alt_index, iface_index = uaa->info.bIfaceIndex;
	int error,unit = device_get_unit(dev);

	sc->sc_dev = dev;
	sc->sc_usb_device = uaa->device;
	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mutex, "uhid_snes", NULL, MTX_DEF | MTX_RECURSE);
	usb_callout_init_mtx(&sc->sc_watchdog, &sc->sc_mutex, 0);

	idesc = usbd_get_interface_descriptor(uaa->iface);
	alt_index = -1;
	for(;;) {
		if (idesc == NULL)
			break;

		if ((idesc->bDescriptorType == UDESC_INTERFACE) &&
		     (idesc->bLength >= sizeof(*idesc))) {
			if (idesc->bInterfaceNumber != uaa->info.bIfaceNum) {
				break;
			} else {
				alt_index++;
				if (idesc->bInterfaceClass == UICLASS_HID)
					goto found;
			}
		}

		cdesc = usbd_get_config_descriptor(uaa->device);
		idesc = (void *)usb_desc_foreach(cdesc, (void *)idesc);
		goto found;
	}
	goto detach;

found:
	if (alt_index) {
		error = usbd_set_alt_interface_index(uaa->device, iface_index, alt_index);
		if (error)
			goto detach;
	}

	sc->sc_iface_num = idesc->bInterfaceNumber;

	error = usbd_transfer_setup(uaa->device, &iface_index,
	    sc->sc_transfer, uhid_snes_config, UHID_SNES_N_TRANSFER, sc,
	    &sc->sc_mutex);

	if (error)
		goto detach;

	error = usb_fifo_attach(uaa->device, sc, &sc->sc_mutex,
	    &uhid_snes_fifo_methods, &sc->sc_fifo, unit, -1,
	    iface_index, UID_ROOT, GID_OPERATOR, 0644);
	sc->sc_repdesc_size = sizeof(uhid_snes_report_descr);
	sc->sc_repdesc_ptr = __DECONST(void*, &uhid_snes_report_descr);


	if (error)
		goto detach;

	mtx_lock(&sc->sc_mutex);
	uhid_snes_watchdog(sc);
	mtx_unlock(&sc->sc_mutex);
	return (0);

detach:
	uhid_snes_detach(dev);
	return (ENOMEM);
}

static int
uhid_snes_detach(device_t dev)
{
	struct uhid_snes_softc *sc = device_get_softc(dev);

	usb_fifo_detach(&sc->sc_fifo);
	usb_fifo_detach(&sc->sc_fifo_no_reset);

	mtx_lock(&sc->sc_mutex);
	usb_callout_stop(&sc->sc_watchdog);
	mtx_unlock(&sc->sc_mutex);

	usbd_transfer_unsetup(sc->sc_transfer, UHID_SNES_N_TRANSFER);
	usb_callout_drain(&sc->sc_watchdog);
	mtx_destroy(&sc->sc_mutex);

	return (0);
}

static device_method_t uhid_snes_methods[] = {
	DEVMETHOD(device_probe, uhid_snes_probe),
	DEVMETHOD(device_attach, uhid_snes_attach),
	DEVMETHOD(device_detach, uhid_snes_detach),
	DEVMETHOD_END
};

static driver_t uhid_snes_driver = {
	"uhid_snes",
	uhid_snes_methods,
	sizeof(struct uhid_snes_softc)
};

static devclass_t uhid_snes_devclass;

DRIVER_MODULE(uhid_snes, uhub, uhid_snes_driver, uhid_snes_devclass, NULL, 0);
MODULE_DEPEND(uhid_snes, usb, 1, 1, 1);
USB_PNP_HOST_INFO(snes_devs);
