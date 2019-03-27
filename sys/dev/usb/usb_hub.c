/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1998 The NetBSD Foundation, Inc. All rights reserved.
 * Copyright (c) 1998 Lennart Augustsson. All rights reserved.
 * Copyright (c) 2008-2010 Hans Petter Selasky. All rights reserved.
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
 * USB spec: http://www.usb.org/developers/docs/usbspec.zip 
 */

#ifdef USB_GLOBAL_INCLUDE_FILE
#include USB_GLOBAL_INCLUDE_FILE
#else
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

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>

#define	USB_DEBUG_VAR uhub_debug

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_device.h>
#include <dev/usb/usb_request.h>
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_hub.h>
#include <dev/usb/usb_util.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_transfer.h>
#include <dev/usb/usb_dynamic.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#endif			/* USB_GLOBAL_INCLUDE_FILE */

#define	UHUB_INTR_INTERVAL 250		/* ms */
enum {
	UHUB_INTR_TRANSFER,
#if USB_HAVE_TT_SUPPORT
	UHUB_RESET_TT_TRANSFER,
#endif
	UHUB_N_TRANSFER,
};

#ifdef USB_DEBUG
static int uhub_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, uhub, CTLFLAG_RW, 0, "USB HUB");
SYSCTL_INT(_hw_usb_uhub, OID_AUTO, debug, CTLFLAG_RWTUN, &uhub_debug, 0,
    "Debug level");
#endif

#if USB_HAVE_POWERD
static int usb_power_timeout = 30;	/* seconds */

SYSCTL_INT(_hw_usb, OID_AUTO, power_timeout, CTLFLAG_RWTUN,
    &usb_power_timeout, 0, "USB power timeout");
#endif

#if USB_HAVE_DISABLE_ENUM
static int usb_disable_enumeration = 0;
SYSCTL_INT(_hw_usb, OID_AUTO, disable_enumeration, CTLFLAG_RWTUN,
    &usb_disable_enumeration, 0, "Set to disable all USB device enumeration. "
	"This can secure against USB devices turning evil, "
	"for example a USB memory stick becoming a USB keyboard.");

static int usb_disable_port_power = 0;
SYSCTL_INT(_hw_usb, OID_AUTO, disable_port_power, CTLFLAG_RWTUN,
    &usb_disable_port_power, 0, "Set to disable all USB port power.");
#endif

struct uhub_current_state {
	uint16_t port_change;
	uint16_t port_status;
};

struct uhub_softc {
	struct uhub_current_state sc_st;/* current state */
#if (USB_HAVE_FIXED_PORT != 0)
	struct usb_hub sc_hub;
#endif
	device_t sc_dev;		/* base device */
	struct mtx sc_mtx;		/* our mutex */
	struct usb_device *sc_udev;	/* USB device */
	struct usb_xfer *sc_xfer[UHUB_N_TRANSFER];	/* interrupt xfer */
#if USB_HAVE_DISABLE_ENUM
	int sc_disable_enumeration;
	int sc_disable_port_power;
#endif
	uint8_t sc_usb_port_errors;	/* error counter */
#define	UHUB_USB_PORT_ERRORS_MAX 4
	uint8_t	sc_flags;
#define	UHUB_FLAG_DID_EXPLORE 0x01
};

#define	UHUB_PROTO(sc) ((sc)->sc_udev->ddesc.bDeviceProtocol)
#define	UHUB_IS_HIGH_SPEED(sc) (UHUB_PROTO(sc) != UDPROTO_FSHUB)
#define	UHUB_IS_SINGLE_TT(sc) (UHUB_PROTO(sc) == UDPROTO_HSHUBSTT)
#define	UHUB_IS_MULTI_TT(sc) (UHUB_PROTO(sc) == UDPROTO_HSHUBMTT)
#define	UHUB_IS_SUPER_SPEED(sc) (UHUB_PROTO(sc) == UDPROTO_SSHUB)

/* prototypes for type checking: */

static device_probe_t uhub_probe;
static device_attach_t uhub_attach;
static device_detach_t uhub_detach;
static device_suspend_t uhub_suspend;
static device_resume_t uhub_resume;

static bus_driver_added_t uhub_driver_added;
static bus_child_location_str_t uhub_child_location_string;
static bus_child_pnpinfo_str_t uhub_child_pnpinfo_string;

static usb_callback_t uhub_intr_callback;
#if USB_HAVE_TT_SUPPORT
static usb_callback_t uhub_reset_tt_callback;
#endif

static void usb_dev_resume_peer(struct usb_device *udev);
static void usb_dev_suspend_peer(struct usb_device *udev);
static uint8_t usb_peer_should_wakeup(struct usb_device *udev);

static const struct usb_config uhub_config[UHUB_N_TRANSFER] = {

	[UHUB_INTR_TRANSFER] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_ANY,
		.timeout = 0,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,	/* use wMaxPacketSize */
		.callback = &uhub_intr_callback,
		.interval = UHUB_INTR_INTERVAL,
	},
#if USB_HAVE_TT_SUPPORT
	[UHUB_RESET_TT_TRANSFER] = {
		.type = UE_CONTROL,
		.endpoint = 0x00,	/* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize = sizeof(struct usb_device_request),
		.callback = &uhub_reset_tt_callback,
		.timeout = 1000,	/* 1 second */
		.usb_mode = USB_MODE_HOST,
	},
#endif
};

/*
 * driver instance for "hub" connected to "usb"
 * and "hub" connected to "hub"
 */
static devclass_t uhub_devclass;

static device_method_t uhub_methods[] = {
	DEVMETHOD(device_probe, uhub_probe),
	DEVMETHOD(device_attach, uhub_attach),
	DEVMETHOD(device_detach, uhub_detach),

	DEVMETHOD(device_suspend, uhub_suspend),
	DEVMETHOD(device_resume, uhub_resume),

	DEVMETHOD(bus_child_location_str, uhub_child_location_string),
	DEVMETHOD(bus_child_pnpinfo_str, uhub_child_pnpinfo_string),
	DEVMETHOD(bus_driver_added, uhub_driver_added),
	DEVMETHOD_END
};

static driver_t uhub_driver = {
	.name = "uhub",
	.methods = uhub_methods,
	.size = sizeof(struct uhub_softc)
};

DRIVER_MODULE(uhub, usbus, uhub_driver, uhub_devclass, 0, 0);
DRIVER_MODULE(uhub, uhub, uhub_driver, uhub_devclass, NULL, 0);
MODULE_VERSION(uhub, 1);

static void
uhub_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uhub_softc *sc = usbd_xfer_softc(xfer);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		DPRINTFN(2, "\n");
		/*
		 * This is an indication that some port
		 * has changed status. Notify the bus
		 * event handler thread that we need
		 * to be explored again:
		 */
		usb_needs_explore(sc->sc_udev->bus, 0);

	case USB_ST_SETUP:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:			/* Error */
		if (xfer->error != USB_ERR_CANCELLED) {
			/*
			 * Do a clear-stall. The "stall_pipe" flag
			 * will get cleared before next callback by
			 * the USB stack.
			 */
			usbd_xfer_set_stall(xfer);
			usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
			usbd_transfer_submit(xfer);
		}
		break;
	}
}

/*------------------------------------------------------------------------*
 *      uhub_reset_tt_proc
 *
 * This function starts the TT reset USB request
 *------------------------------------------------------------------------*/
#if USB_HAVE_TT_SUPPORT
static void
uhub_reset_tt_proc(struct usb_proc_msg *_pm)
{
	struct usb_udev_msg *pm = (void *)_pm;
	struct usb_device *udev = pm->udev;
	struct usb_hub *hub;
	struct uhub_softc *sc;

	hub = udev->hub;
	if (hub == NULL)
		return;
	sc = hub->hubsoftc;
	if (sc == NULL)
		return;

	/* Change lock */
	USB_BUS_UNLOCK(udev->bus);
	USB_MTX_LOCK(&sc->sc_mtx);
	/* Start transfer */
	usbd_transfer_start(sc->sc_xfer[UHUB_RESET_TT_TRANSFER]);
	/* Change lock */
	USB_MTX_UNLOCK(&sc->sc_mtx);
	USB_BUS_LOCK(udev->bus);
}
#endif

/*------------------------------------------------------------------------*
 *      uhub_tt_buffer_reset_async_locked
 *
 * This function queues a TT reset for the given USB device and endpoint.
 *------------------------------------------------------------------------*/
#if USB_HAVE_TT_SUPPORT
void
uhub_tt_buffer_reset_async_locked(struct usb_device *child, struct usb_endpoint *ep)
{
	struct usb_device_request req;
	struct usb_device *udev;
	struct usb_hub *hub;
	struct usb_port *up;
	uint16_t wValue;
	uint8_t port;

	if (child == NULL || ep == NULL)
		return;

	udev = child->parent_hs_hub;
	port = child->hs_port_no;

	if (udev == NULL)
		return;

	hub = udev->hub;
	if ((hub == NULL) ||
	    (udev->speed != USB_SPEED_HIGH) ||
	    (child->speed != USB_SPEED_LOW &&
	     child->speed != USB_SPEED_FULL) ||
	    (child->flags.usb_mode != USB_MODE_HOST) ||
	    (port == 0) || (ep->edesc == NULL)) {
		/* not applicable */
		return;
	}

	USB_BUS_LOCK_ASSERT(udev->bus, MA_OWNED);

	up = hub->ports + port - 1;

	if (udev->ddesc.bDeviceClass == UDCLASS_HUB &&
	    udev->ddesc.bDeviceProtocol == UDPROTO_HSHUBSTT)
		port = 1;

	/* if we already received a clear buffer request, reset the whole TT */
	if (up->req_reset_tt.bRequest != 0) {
		req.bmRequestType = UT_WRITE_CLASS_OTHER;
		req.bRequest = UR_RESET_TT;
		USETW(req.wValue, 0);
		req.wIndex[0] = port;
		req.wIndex[1] = 0;
		USETW(req.wLength, 0);
	} else {
		wValue = (ep->edesc->bEndpointAddress & 0xF) |
		      ((child->address & 0x7F) << 4) |
		      ((ep->edesc->bEndpointAddress & 0x80) << 8) |
		      ((ep->edesc->bmAttributes & 3) << 12);

		req.bmRequestType = UT_WRITE_CLASS_OTHER;
		req.bRequest = UR_CLEAR_TT_BUFFER;
		USETW(req.wValue, wValue);
		req.wIndex[0] = port;
		req.wIndex[1] = 0;
		USETW(req.wLength, 0);
	}
	up->req_reset_tt = req;
	/* get reset transfer started */
	usb_proc_msignal(USB_BUS_TT_PROC(udev->bus),
	    &hub->tt_msg[0], &hub->tt_msg[1]);
}
#endif

#if USB_HAVE_TT_SUPPORT
static void
uhub_reset_tt_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct uhub_softc *sc;
	struct usb_device *udev;
	struct usb_port *up;
	uint8_t x;

	DPRINTF("TT buffer reset\n");

	sc = usbd_xfer_softc(xfer);
	udev = sc->sc_udev;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
	case USB_ST_SETUP:
tr_setup:
		USB_BUS_LOCK(udev->bus);
		/* find first port which needs a TT reset */
		for (x = 0; x != udev->hub->nports; x++) {
			up = udev->hub->ports + x;

			if (up->req_reset_tt.bRequest == 0)
				continue;

			/* copy in the transfer */
			usbd_copy_in(xfer->frbuffers, 0, &up->req_reset_tt,
			    sizeof(up->req_reset_tt));
			/* reset buffer */
			memset(&up->req_reset_tt, 0, sizeof(up->req_reset_tt));

			/* set length */
			usbd_xfer_set_frame_len(xfer, 0, sizeof(up->req_reset_tt));
			xfer->nframes = 1;
			USB_BUS_UNLOCK(udev->bus);

			usbd_transfer_submit(xfer);
			return;
		}
		USB_BUS_UNLOCK(udev->bus);
		break;

	default:
		if (error == USB_ERR_CANCELLED)
			break;

		DPRINTF("TT buffer reset failed (%s)\n", usbd_errstr(error));
		goto tr_setup;
	}
}
#endif

/*------------------------------------------------------------------------*
 *      uhub_count_active_host_ports
 *
 * This function counts the number of active ports at the given speed.
 *------------------------------------------------------------------------*/
uint8_t
uhub_count_active_host_ports(struct usb_device *udev, enum usb_dev_speed speed)
{
	struct uhub_softc *sc;
	struct usb_device *child;
	struct usb_hub *hub;
	struct usb_port *up;
	uint8_t retval = 0;
	uint8_t x;

	if (udev == NULL)
		goto done;
	hub = udev->hub;
	if (hub == NULL)
		goto done;
	sc = hub->hubsoftc;
	if (sc == NULL)
		goto done;

	for (x = 0; x != hub->nports; x++) {
		up = hub->ports + x;
		child = usb_bus_port_get_device(udev->bus, up);
		if (child != NULL &&
		    child->flags.usb_mode == USB_MODE_HOST &&
		    child->speed == speed)
			retval++;
	}
done:
	return (retval);
}

void
uhub_explore_handle_re_enumerate(struct usb_device *child)
{
	uint8_t do_unlock;
	usb_error_t err;

	/* check if device should be re-enumerated */
	if (child->flags.usb_mode != USB_MODE_HOST)
		return;

	do_unlock = usbd_enum_lock(child);
	switch (child->re_enumerate_wait) {
	case USB_RE_ENUM_START:
		err = usbd_set_config_index(child,
		    USB_UNCONFIG_INDEX);
		if (err != 0) {
			DPRINTF("Unconfigure failed: %s: Ignored.\n",
			    usbd_errstr(err));
		}
		if (child->parent_hub == NULL) {
			/* the root HUB cannot be re-enumerated */
			DPRINTFN(6, "cannot reset root HUB\n");
			err = 0;
		} else {
			err = usbd_req_re_enumerate(child, NULL);
		}
		if (err == 0)
			err = usbd_set_config_index(child, 0);
		if (err == 0) {
			err = usb_probe_and_attach(child,
			    USB_IFACE_INDEX_ANY);
		}
		child->re_enumerate_wait = USB_RE_ENUM_DONE;
		break;

	case USB_RE_ENUM_PWR_OFF:
		/* get the device unconfigured */
		err = usbd_set_config_index(child,
		    USB_UNCONFIG_INDEX);
		if (err) {
			DPRINTFN(0, "Could not unconfigure "
			    "device (ignored)\n");
		}
		if (child->parent_hub == NULL) {
			/* the root HUB cannot be re-enumerated */
			DPRINTFN(6, "cannot set port feature\n");
			err = 0;
		} else {
			/* clear port enable */
			err = usbd_req_clear_port_feature(child->parent_hub,
			    NULL, child->port_no, UHF_PORT_ENABLE);
			if (err) {
				DPRINTFN(0, "Could not disable port "
				    "(ignored)\n");
			}
		}
		child->re_enumerate_wait = USB_RE_ENUM_DONE;
		break;

	case USB_RE_ENUM_SET_CONFIG:
		err = usbd_set_config_index(child,
		    child->next_config_index);
		if (err != 0) {
			DPRINTF("Configure failed: %s: Ignored.\n",
			    usbd_errstr(err));
		} else {
			err = usb_probe_and_attach(child,
			    USB_IFACE_INDEX_ANY);
		}
		child->re_enumerate_wait = USB_RE_ENUM_DONE;
		break;

	default:
		child->re_enumerate_wait = USB_RE_ENUM_DONE;
		break;
	}
	if (do_unlock)
		usbd_enum_unlock(child);
}

/*------------------------------------------------------------------------*
 *	uhub_explore_sub - subroutine
 *
 * Return values:
 *    0: Success
 * Else: A control transaction failed
 *------------------------------------------------------------------------*/
static usb_error_t
uhub_explore_sub(struct uhub_softc *sc, struct usb_port *up)
{
	struct usb_bus *bus;
	struct usb_device *child;
	uint8_t refcount;
	usb_error_t err;

	bus = sc->sc_udev->bus;
	err = 0;

	/* get driver added refcount from USB bus */
	refcount = bus->driver_added_refcount;

	/* get device assosiated with the given port */
	child = usb_bus_port_get_device(bus, up);
	if (child == NULL) {
		/* nothing to do */
		goto done;
	}

	uhub_explore_handle_re_enumerate(child);

	/* check if probe and attach should be done */

	if (child->driver_added_refcount != refcount) {
		child->driver_added_refcount = refcount;
		err = usb_probe_and_attach(child,
		    USB_IFACE_INDEX_ANY);
		if (err) {
			goto done;
		}
	}
	/* start control transfer, if device mode */

	if (child->flags.usb_mode == USB_MODE_DEVICE)
		usbd_ctrl_transfer_setup(child);

	/* if a HUB becomes present, do a recursive HUB explore */

	if (child->hub)
		err = (child->hub->explore) (child);

done:
	return (err);
}

/*------------------------------------------------------------------------*
 *	uhub_read_port_status - factored out code
 *------------------------------------------------------------------------*/
static usb_error_t
uhub_read_port_status(struct uhub_softc *sc, uint8_t portno)
{
	struct usb_port_status ps;
	usb_error_t err;

	if (sc->sc_usb_port_errors >= UHUB_USB_PORT_ERRORS_MAX) {
		DPRINTFN(4, "port %d, HUB looks dead, too many errors\n", portno);
		sc->sc_st.port_status = 0;
		sc->sc_st.port_change = 0;
		return (USB_ERR_TIMEOUT);
	}

	err = usbd_req_get_port_status(
	    sc->sc_udev, NULL, &ps, portno);

	if (err == 0) {
		sc->sc_st.port_status = UGETW(ps.wPortStatus);
		sc->sc_st.port_change = UGETW(ps.wPortChange);
		sc->sc_usb_port_errors = 0;
	} else {
		sc->sc_st.port_status = 0;
		sc->sc_st.port_change = 0;
		sc->sc_usb_port_errors++;
	}

	/* debugging print */

	DPRINTFN(4, "port %d, wPortStatus=0x%04x, "
	    "wPortChange=0x%04x, err=%s\n",
	    portno, sc->sc_st.port_status,
	    sc->sc_st.port_change, usbd_errstr(err));
	return (err);
}

/*------------------------------------------------------------------------*
 *	uhub_reattach_port
 *
 * Returns:
 *    0: Success
 * Else: A control transaction failed
 *------------------------------------------------------------------------*/
static usb_error_t
uhub_reattach_port(struct uhub_softc *sc, uint8_t portno)
{
	struct usb_device *child;
	struct usb_device *udev;
	enum usb_dev_speed speed;
	enum usb_hc_mode mode;
	usb_error_t err;
	uint16_t power_mask;
	uint8_t timeout;

	DPRINTF("reattaching port %d\n", portno);

	timeout = 0;
	udev = sc->sc_udev;
	child = usb_bus_port_get_device(udev->bus,
	    udev->hub->ports + portno - 1);

repeat:

	/* first clear the port connection change bit */

	err = usbd_req_clear_port_feature(udev, NULL,
	    portno, UHF_C_PORT_CONNECTION);

	if (err)
		goto error;

	/* check if there is a child */

	if (child != NULL) {
		/*
		 * Free USB device and all subdevices, if any.
		 */
		usb_free_device(child, 0);
		child = NULL;
	}
	/* get fresh status */

	err = uhub_read_port_status(sc, portno);
	if (err)
		goto error;

#if USB_HAVE_DISABLE_ENUM
	/* check if we should skip enumeration from this USB HUB */
	if (usb_disable_enumeration != 0 ||
	    sc->sc_disable_enumeration != 0) {
		DPRINTF("Enumeration is disabled!\n");
		goto error;
	}
#endif
	/* check if nothing is connected to the port */

	if (!(sc->sc_st.port_status & UPS_CURRENT_CONNECT_STATUS))
		goto error;

	/* check if there is no power on the port and print a warning */

	switch (udev->speed) {
	case USB_SPEED_HIGH:
	case USB_SPEED_FULL:
	case USB_SPEED_LOW:
		power_mask = UPS_PORT_POWER;
		break;
	case USB_SPEED_SUPER:
		if (udev->parent_hub == NULL)
			power_mask = UPS_PORT_POWER;
		else
			power_mask = UPS_PORT_POWER_SS;
		break;
	default:
		power_mask = 0;
		break;
	}
	if (!(sc->sc_st.port_status & power_mask)) {
		DPRINTF("WARNING: strange, connected port %d "
		    "has no power\n", portno);
	}

	/* check if the device is in Host Mode */

	if (!(sc->sc_st.port_status & UPS_PORT_MODE_DEVICE)) {

		DPRINTF("Port %d is in Host Mode\n", portno);

		if (sc->sc_st.port_status & UPS_SUSPEND) {
			/*
			 * NOTE: Should not get here in SuperSpeed
			 * mode, because the HUB should report this
			 * bit as zero.
			 */
			DPRINTF("Port %d was still "
			    "suspended, clearing.\n", portno);
			err = usbd_req_clear_port_feature(udev,
			    NULL, portno, UHF_PORT_SUSPEND);
		}

		/* USB Host Mode */

		/* wait for maximum device power up time */

		usb_pause_mtx(NULL, 
		    USB_MS_TO_TICKS(usb_port_powerup_delay));

		/* reset port, which implies enabling it */

		err = usbd_req_reset_port(udev, NULL, portno);

		if (err) {
			DPRINTFN(0, "port %d reset "
			    "failed, error=%s\n",
			    portno, usbd_errstr(err));
			goto error;
		}
		/* get port status again, it might have changed during reset */

		err = uhub_read_port_status(sc, portno);
		if (err) {
			goto error;
		}
		/* check if something changed during port reset */

		if ((sc->sc_st.port_change & UPS_C_CONNECT_STATUS) ||
		    (!(sc->sc_st.port_status & UPS_CURRENT_CONNECT_STATUS))) {
			if (timeout) {
				DPRINTFN(0, "giving up port reset "
				    "- device vanished\n");
				goto error;
			}
			timeout = 1;
			goto repeat;
		}
	} else {
		DPRINTF("Port %d is in Device Mode\n", portno);
	}

	/*
	 * Figure out the device speed
	 */
	switch (udev->speed) {
	case USB_SPEED_HIGH:
		if (sc->sc_st.port_status & UPS_HIGH_SPEED)
			speed = USB_SPEED_HIGH;
		else if (sc->sc_st.port_status & UPS_LOW_SPEED)
			speed = USB_SPEED_LOW;
		else
			speed = USB_SPEED_FULL;
		break;
	case USB_SPEED_FULL:
		if (sc->sc_st.port_status & UPS_LOW_SPEED)
			speed = USB_SPEED_LOW;
		else
			speed = USB_SPEED_FULL;
		break;
	case USB_SPEED_LOW:
		speed = USB_SPEED_LOW;
		break;
	case USB_SPEED_SUPER:
		if (udev->parent_hub == NULL) {
			/* Root HUB - special case */
			switch (sc->sc_st.port_status & UPS_OTHER_SPEED) {
			case 0:
				speed = USB_SPEED_FULL;
				break;
			case UPS_LOW_SPEED:
				speed = USB_SPEED_LOW;
				break;
			case UPS_HIGH_SPEED:
				speed = USB_SPEED_HIGH;
				break;
			default:
				speed = USB_SPEED_SUPER;
				break;
			}
		} else {
			speed = USB_SPEED_SUPER;
		}
		break;
	default:
		/* same speed like parent */
		speed = udev->speed;
		break;
	}
	if (speed == USB_SPEED_SUPER) {
		err = usbd_req_set_hub_u1_timeout(udev, NULL,
		    portno, 128 - (2 * udev->depth));
		if (err) {
			DPRINTFN(0, "port %d U1 timeout "
			    "failed, error=%s\n",
			    portno, usbd_errstr(err));
		}
		err = usbd_req_set_hub_u2_timeout(udev, NULL,
		    portno, 128 - (2 * udev->depth));
		if (err) {
			DPRINTFN(0, "port %d U2 timeout "
			    "failed, error=%s\n",
			    portno, usbd_errstr(err));
		}
	}

	/*
	 * Figure out the device mode
	 *
	 * NOTE: This part is currently FreeBSD specific.
	 */
	if (udev->parent_hub != NULL) {
		/* inherit mode from the parent HUB */
		mode = udev->parent_hub->flags.usb_mode;
	} else if (sc->sc_st.port_status & UPS_PORT_MODE_DEVICE)
		mode = USB_MODE_DEVICE;
	else
		mode = USB_MODE_HOST;

	/* need to create a new child */
	child = usb_alloc_device(sc->sc_dev, udev->bus, udev,
	    udev->depth + 1, portno - 1, portno, speed, mode);
	if (child == NULL) {
		DPRINTFN(0, "could not allocate new device\n");
		goto error;
	}
	return (0);			/* success */

error:
	if (child != NULL) {
		/*
		 * Free USB device and all subdevices, if any.
		 */
		usb_free_device(child, 0);
		child = NULL;
	}
	if (err == 0) {
		if (sc->sc_st.port_status & UPS_PORT_ENABLED) {
			err = usbd_req_clear_port_feature(
			    sc->sc_udev, NULL,
			    portno, UHF_PORT_ENABLE);
		}
	}
	if (err) {
		DPRINTFN(0, "device problem (%s), "
		    "disabling port %d\n", usbd_errstr(err), portno);
	}
	return (err);
}

/*------------------------------------------------------------------------*
 *	usb_device_20_compatible
 *
 * Returns:
 *    0: HUB does not support suspend and resume
 * Else: HUB supports suspend and resume
 *------------------------------------------------------------------------*/
static uint8_t
usb_device_20_compatible(struct usb_device *udev)
{
	if (udev == NULL)
		return (0);
	switch (udev->speed) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
	case USB_SPEED_HIGH:
		return (1);
	default:
		return (0);
	}
}

/*------------------------------------------------------------------------*
 *	uhub_suspend_resume_port
 *
 * Returns:
 *    0: Success
 * Else: A control transaction failed
 *------------------------------------------------------------------------*/
static usb_error_t
uhub_suspend_resume_port(struct uhub_softc *sc, uint8_t portno)
{
	struct usb_device *child;
	struct usb_device *udev;
	uint8_t is_suspend;
	usb_error_t err;

	DPRINTF("port %d\n", portno);

	udev = sc->sc_udev;
	child = usb_bus_port_get_device(udev->bus,
	    udev->hub->ports + portno - 1);

	/* first clear the port suspend change bit */

	if (usb_device_20_compatible(udev)) {
		err = usbd_req_clear_port_feature(udev, NULL,
		    portno, UHF_C_PORT_SUSPEND);
	} else {
		err = usbd_req_clear_port_feature(udev, NULL,
		    portno, UHF_C_PORT_LINK_STATE);
	}

	if (err) {
		DPRINTF("clearing suspend failed.\n");
		goto done;
	}
	/* get fresh status */

	err = uhub_read_port_status(sc, portno);
	if (err) {
		DPRINTF("reading port status failed.\n");
		goto done;
	}
	/* convert current state */

	if (usb_device_20_compatible(udev)) {
		if (sc->sc_st.port_status & UPS_SUSPEND) {
			is_suspend = 1;
		} else {
			is_suspend = 0;
		}
	} else {
		switch (UPS_PORT_LINK_STATE_GET(sc->sc_st.port_status)) {
		case UPS_PORT_LS_U3:
			is_suspend = 1;
			break;
		case UPS_PORT_LS_SS_INA:
			usbd_req_warm_reset_port(udev, NULL, portno);
			is_suspend = 0;
			break;
		default:
			is_suspend = 0;
			break;
		}
	}

	DPRINTF("suspended=%u\n", is_suspend);

	/* do the suspend or resume */

	if (child) {
		/*
		 * This code handle two cases: 1) Host Mode - we can only
		 * receive resume here 2) Device Mode - we can receive
		 * suspend and resume here
		 */
		if (is_suspend == 0)
			usb_dev_resume_peer(child);
		else if (child->flags.usb_mode == USB_MODE_DEVICE)
			usb_dev_suspend_peer(child);
	}
done:
	return (err);
}

/*------------------------------------------------------------------------*
 *	uhub_root_interrupt
 *
 * This function is called when a Root HUB interrupt has
 * happened. "ptr" and "len" makes up the Root HUB interrupt
 * packet. This function is called having the "bus_mtx" locked.
 *------------------------------------------------------------------------*/
void
uhub_root_intr(struct usb_bus *bus, const uint8_t *ptr, uint8_t len)
{
	USB_BUS_LOCK_ASSERT(bus, MA_OWNED);

	usb_needs_explore(bus, 0);
}

static uint8_t
uhub_is_too_deep(struct usb_device *udev)
{
	switch (udev->speed) {
	case USB_SPEED_FULL:
	case USB_SPEED_LOW:
	case USB_SPEED_HIGH:
		if (udev->depth > USB_HUB_MAX_DEPTH)
			return (1);
		break;
	case USB_SPEED_SUPER:
		if (udev->depth > USB_SS_HUB_DEPTH_MAX)
			return (1);
		break;
	default:
		break;
	}
	return (0);
}

/*------------------------------------------------------------------------*
 *	uhub_explore
 *
 * Returns:
 *     0: Success
 *  Else: Failure
 *------------------------------------------------------------------------*/
static usb_error_t
uhub_explore(struct usb_device *udev)
{
	struct usb_hub *hub;
	struct uhub_softc *sc;
	struct usb_port *up;
	usb_error_t err;
	uint8_t portno;
	uint8_t x;
	uint8_t do_unlock;

	hub = udev->hub;
	sc = hub->hubsoftc;

	DPRINTFN(11, "udev=%p addr=%d\n", udev, udev->address);

	/* ignore devices that are too deep */
	if (uhub_is_too_deep(udev))
		return (USB_ERR_TOO_DEEP);

	/* check if device is suspended */
	if (udev->flags.self_suspended) {
		/* need to wait until the child signals resume */
		DPRINTF("Device is suspended!\n");
		return (0);
	}

	/*
	 * Make sure we don't race against user-space applications
	 * like LibUSB:
	 */
	do_unlock = usbd_enum_lock(udev);

	for (x = 0; x != hub->nports; x++) {
		up = hub->ports + x;
		portno = x + 1;

		err = uhub_read_port_status(sc, portno);
		if (err) {
			/* most likely the HUB is gone */
			break;
		}
		if (sc->sc_st.port_change & UPS_C_OVERCURRENT_INDICATOR) {
			DPRINTF("Overcurrent on port %u.\n", portno);
			err = usbd_req_clear_port_feature(
			    udev, NULL, portno, UHF_C_PORT_OVER_CURRENT);
			if (err) {
				/* most likely the HUB is gone */
				break;
			}
		}
		if (!(sc->sc_flags & UHUB_FLAG_DID_EXPLORE)) {
			/*
			 * Fake a connect status change so that the
			 * status gets checked initially!
			 */
			sc->sc_st.port_change |=
			    UPS_C_CONNECT_STATUS;
		}
		if (sc->sc_st.port_change & UPS_C_PORT_ENABLED) {
			err = usbd_req_clear_port_feature(
			    udev, NULL, portno, UHF_C_PORT_ENABLE);
			if (err) {
				/* most likely the HUB is gone */
				break;
			}
			if (sc->sc_st.port_change & UPS_C_CONNECT_STATUS) {
				/*
				 * Ignore the port error if the device
				 * has vanished !
				 */
			} else if (sc->sc_st.port_status & UPS_PORT_ENABLED) {
				DPRINTFN(0, "illegal enable change, "
				    "port %d\n", portno);
			} else {

				if (up->restartcnt == USB_RESTART_MAX) {
					/* XXX could try another speed ? */
					DPRINTFN(0, "port error, giving up "
					    "port %d\n", portno);
				} else {
					sc->sc_st.port_change |=
					    UPS_C_CONNECT_STATUS;
					up->restartcnt++;
				}
			}
		}
		if (sc->sc_st.port_change & UPS_C_CONNECT_STATUS) {
			err = uhub_reattach_port(sc, portno);
			if (err) {
				/* most likely the HUB is gone */
				break;
			}
		}
		if (sc->sc_st.port_change & (UPS_C_SUSPEND |
		    UPS_C_PORT_LINK_STATE)) {
			err = uhub_suspend_resume_port(sc, portno);
			if (err) {
				/* most likely the HUB is gone */
				break;
			}
		}
		err = uhub_explore_sub(sc, up);
		if (err) {
			/* no device(s) present */
			continue;
		}
		/* explore succeeded - reset restart counter */
		up->restartcnt = 0;
	}

	if (do_unlock)
		usbd_enum_unlock(udev);

	/* initial status checked */
	sc->sc_flags |= UHUB_FLAG_DID_EXPLORE;

	/* return success */
	return (USB_ERR_NORMAL_COMPLETION);
}

static int
uhub_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);

	/*
	 * The subclass for USB HUBs is currently ignored because it
	 * is 0 for some and 1 for others.
	 */
	if (uaa->info.bConfigIndex == 0 &&
	    uaa->info.bDeviceClass == UDCLASS_HUB)
		return (0);

	return (ENXIO);
}

/* NOTE: The information returned by this function can be wrong. */
usb_error_t
uhub_query_info(struct usb_device *udev, uint8_t *pnports, uint8_t *ptt)
{
	struct usb_hub_descriptor hubdesc20;
	struct usb_hub_ss_descriptor hubdesc30;
	usb_error_t err;
	uint8_t nports;
	uint8_t tt;

	if (udev->ddesc.bDeviceClass != UDCLASS_HUB)
		return (USB_ERR_INVAL);

	nports = 0;
	tt = 0;

	switch (udev->speed) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
	case USB_SPEED_HIGH:
		/* assuming that there is one port */
		err = usbd_req_get_hub_descriptor(udev, NULL, &hubdesc20, 1);
		if (err) {
			DPRINTFN(0, "getting USB 2.0 HUB descriptor failed,"
			    "error=%s\n", usbd_errstr(err));
			break;
		}
		nports = hubdesc20.bNbrPorts;
		if (nports > 127)
			nports = 127;

		if (udev->speed == USB_SPEED_HIGH)
			tt = (UGETW(hubdesc20.wHubCharacteristics) >> 5) & 3;
		break;

	case USB_SPEED_SUPER:
		err = usbd_req_get_ss_hub_descriptor(udev, NULL, &hubdesc30, 1);
		if (err) {
			DPRINTFN(0, "Getting USB 3.0 HUB descriptor failed,"
			    "error=%s\n", usbd_errstr(err));
			break;
		}
		nports = hubdesc30.bNbrPorts;
		if (nports > 16)
			nports = 16;
		break;

	default:
		err = USB_ERR_INVAL;
		break;
	}

	if (pnports != NULL)
		*pnports = nports;

	if (ptt != NULL)
		*ptt = tt;

	return (err);
}

static int
uhub_attach(device_t dev)
{
	struct uhub_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct usb_device *udev = uaa->device;
	struct usb_device *parent_hub = udev->parent_hub;
	struct usb_hub *hub;
	struct usb_hub_descriptor hubdesc20;
	struct usb_hub_ss_descriptor hubdesc30;
#if USB_HAVE_DISABLE_ENUM
	struct sysctl_ctx_list *sysctl_ctx;
	struct sysctl_oid *sysctl_tree;
#endif
	uint16_t pwrdly;
	uint16_t nports;
	uint8_t x;
	uint8_t portno;
	uint8_t removable;
	uint8_t iface_index;
	usb_error_t err;

	sc->sc_udev = udev;
	sc->sc_dev = dev;

	mtx_init(&sc->sc_mtx, "USB HUB mutex", NULL, MTX_DEF);

	device_set_usb_desc(dev);

	DPRINTFN(2, "depth=%d selfpowered=%d, parent=%p, "
	    "parent->selfpowered=%d\n",
	    udev->depth,
	    udev->flags.self_powered,
	    parent_hub,
	    parent_hub ?
	    parent_hub->flags.self_powered : 0);

	if (uhub_is_too_deep(udev)) {
		DPRINTFN(0, "HUB at depth %d, "
		    "exceeds maximum. HUB ignored\n", (int)udev->depth);
		goto error;
	}

	if (!udev->flags.self_powered && parent_hub &&
	    !parent_hub->flags.self_powered) {
		DPRINTFN(0, "Bus powered HUB connected to "
		    "bus powered HUB. HUB ignored\n");
		goto error;
	}

	if (UHUB_IS_MULTI_TT(sc)) {
		err = usbd_set_alt_interface_index(udev, 0, 1);
		if (err) {
			device_printf(dev, "MTT could not be enabled\n");
			goto error;
		}
		device_printf(dev, "MTT enabled\n");
	}

	/* get HUB descriptor */

	DPRINTFN(2, "Getting HUB descriptor\n");

	switch (udev->speed) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
	case USB_SPEED_HIGH:
		/* assuming that there is one port */
		err = usbd_req_get_hub_descriptor(udev, NULL, &hubdesc20, 1);
		if (err) {
			DPRINTFN(0, "getting USB 2.0 HUB descriptor failed,"
			    "error=%s\n", usbd_errstr(err));
			goto error;
		}
		/* get number of ports */
		nports = hubdesc20.bNbrPorts;

		/* get power delay */
		pwrdly = ((hubdesc20.bPwrOn2PwrGood * UHD_PWRON_FACTOR) +
		    usb_extra_power_up_time);

		/* get complete HUB descriptor */
		if (nports >= 8) {
			/* check number of ports */
			if (nports > 127) {
				DPRINTFN(0, "Invalid number of USB 2.0 ports,"
				    "error=%s\n", usbd_errstr(err));
				goto error;
			}
			/* get complete HUB descriptor */
			err = usbd_req_get_hub_descriptor(udev, NULL, &hubdesc20, nports);

			if (err) {
				DPRINTFN(0, "Getting USB 2.0 HUB descriptor failed,"
				    "error=%s\n", usbd_errstr(err));
				goto error;
			}
			if (hubdesc20.bNbrPorts != nports) {
				DPRINTFN(0, "Number of ports changed\n");
				goto error;
			}
		}
		break;
	case USB_SPEED_SUPER:
		if (udev->parent_hub != NULL) {
			err = usbd_req_set_hub_depth(udev, NULL,
			    udev->depth - 1);
			if (err) {
				DPRINTFN(0, "Setting USB 3.0 HUB depth failed,"
				    "error=%s\n", usbd_errstr(err));
				goto error;
			}
		}
		err = usbd_req_get_ss_hub_descriptor(udev, NULL, &hubdesc30, 1);
		if (err) {
			DPRINTFN(0, "Getting USB 3.0 HUB descriptor failed,"
			    "error=%s\n", usbd_errstr(err));
			goto error;
		}
		/* get number of ports */
		nports = hubdesc30.bNbrPorts;

		/* get power delay */
		pwrdly = ((hubdesc30.bPwrOn2PwrGood * UHD_PWRON_FACTOR) +
		    usb_extra_power_up_time);

		/* get complete HUB descriptor */
		if (nports >= 8) {
			/* check number of ports */
			if (nports > ((udev->parent_hub != NULL) ? 15 : 127)) {
				DPRINTFN(0, "Invalid number of USB 3.0 ports,"
				    "error=%s\n", usbd_errstr(err));
				goto error;
			}
			/* get complete HUB descriptor */
			err = usbd_req_get_ss_hub_descriptor(udev, NULL, &hubdesc30, nports);

			if (err) {
				DPRINTFN(0, "Getting USB 2.0 HUB descriptor failed,"
				    "error=%s\n", usbd_errstr(err));
				goto error;
			}
			if (hubdesc30.bNbrPorts != nports) {
				DPRINTFN(0, "Number of ports changed\n");
				goto error;
			}
		}
		break;
	default:
		DPRINTF("Assuming HUB has only one port\n");
		/* default number of ports */
		nports = 1;
		/* default power delay */
		pwrdly = ((10 * UHD_PWRON_FACTOR) + usb_extra_power_up_time);
		break;
	}
	if (nports == 0) {
		DPRINTFN(0, "portless HUB\n");
		goto error;
	}
	if (nports > USB_MAX_PORTS) {
		DPRINTF("Port limit exceeded\n");
		goto error;
	}
#if (USB_HAVE_FIXED_PORT == 0)
	hub = malloc(sizeof(hub[0]) + (sizeof(hub->ports[0]) * nports),
	    M_USBDEV, M_WAITOK | M_ZERO);

	if (hub == NULL)
		goto error;
#else
	hub = &sc->sc_hub;
#endif
	udev->hub = hub;

	/* initialize HUB structure */
	hub->hubsoftc = sc;
	hub->explore = &uhub_explore;
	hub->nports = nports;
	hub->hubudev = udev;
#if USB_HAVE_TT_SUPPORT
	hub->tt_msg[0].hdr.pm_callback = &uhub_reset_tt_proc;
	hub->tt_msg[0].udev = udev;
	hub->tt_msg[1].hdr.pm_callback = &uhub_reset_tt_proc;
	hub->tt_msg[1].udev = udev;
#endif
	/* if self powered hub, give ports maximum current */
	if (udev->flags.self_powered) {
		hub->portpower = USB_MAX_POWER;
	} else {
		hub->portpower = USB_MIN_POWER;
	}

	/* set up interrupt pipe */
	iface_index = 0;
	if (udev->parent_hub == NULL) {
		/* root HUB is special */
		err = 0;
	} else {
		/* normal HUB */
		err = usbd_transfer_setup(udev, &iface_index, sc->sc_xfer,
		    uhub_config, UHUB_N_TRANSFER, sc, &sc->sc_mtx);
	}
	if (err) {
		DPRINTFN(0, "cannot setup interrupt transfer, "
		    "errstr=%s\n", usbd_errstr(err));
		goto error;
	}
	/* wait with power off for a while */
	usb_pause_mtx(NULL, USB_MS_TO_TICKS(USB_POWER_DOWN_TIME));

#if USB_HAVE_DISABLE_ENUM
	/* Add device sysctls */

	sysctl_ctx = device_get_sysctl_ctx(dev);
	sysctl_tree = device_get_sysctl_tree(dev);

	if (sysctl_ctx != NULL && sysctl_tree != NULL) {
		(void) SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
		    OID_AUTO, "disable_enumeration", CTLFLAG_RWTUN,
		    &sc->sc_disable_enumeration, 0,
		    "Set to disable enumeration on this USB HUB.");

		(void) SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
		    OID_AUTO, "disable_port_power", CTLFLAG_RWTUN,
		    &sc->sc_disable_port_power, 0,
		    "Set to disable USB port power on this USB HUB.");
	}
#endif
	/*
	 * To have the best chance of success we do things in the exact same
	 * order as Windoze98.  This should not be necessary, but some
	 * devices do not follow the USB specs to the letter.
	 *
	 * These are the events on the bus when a hub is attached:
	 *  Get device and config descriptors (see attach code)
	 *  Get hub descriptor (see above)
	 *  For all ports
	 *     turn on power
	 *     wait for power to become stable
	 * (all below happens in explore code)
	 *  For all ports
	 *     clear C_PORT_CONNECTION
	 *  For all ports
	 *     get port status
	 *     if device connected
	 *        wait 100 ms
	 *        turn on reset
	 *        wait
	 *        clear C_PORT_RESET
	 *        get port status
	 *        proceed with device attachment
	 */

	/* XXX should check for none, individual, or ganged power? */

	removable = 0;

	for (x = 0; x != nports; x++) {
		/* set up data structures */
		struct usb_port *up = hub->ports + x;

		up->device_index = 0;
		up->restartcnt = 0;
		portno = x + 1;

		/* check if port is removable */
		switch (udev->speed) {
		case USB_SPEED_LOW:
		case USB_SPEED_FULL:
		case USB_SPEED_HIGH:
			if (!UHD_NOT_REMOV(&hubdesc20, portno))
				removable++;
			break;
		case USB_SPEED_SUPER:
			if (!UHD_NOT_REMOV(&hubdesc30, portno))
				removable++;
			break;
		default:
			DPRINTF("Assuming removable port\n");
			removable++;
			break;
		}
		if (err == 0) {
#if USB_HAVE_DISABLE_ENUM
			/* check if we should disable USB port power or not */
			if (usb_disable_port_power != 0 ||
			    sc->sc_disable_port_power != 0) {
				/* turn the power off */
				DPRINTFN(2, "Turning port %d power off\n", portno);
				err = usbd_req_clear_port_feature(udev, NULL,
				    portno, UHF_PORT_POWER);
			} else {
#endif
				/* turn the power on */
				DPRINTFN(2, "Turning port %d power on\n", portno);
				err = usbd_req_set_port_feature(udev, NULL,
				    portno, UHF_PORT_POWER);
#if USB_HAVE_DISABLE_ENUM
			}
#endif
		}
		if (err != 0) {
			DPRINTFN(0, "port %d power on or off failed, %s\n",
			    portno, usbd_errstr(err));
		}
		DPRINTF("turn on port %d power\n",
		    portno);

		/* wait for stable power */
		usb_pause_mtx(NULL, USB_MS_TO_TICKS(pwrdly));
	}

	device_printf(dev, "%d port%s with %d "
	    "removable, %s powered\n", nports, (nports != 1) ? "s" : "",
	    removable, udev->flags.self_powered ? "self" : "bus");

	/* Start the interrupt endpoint, if any */

	USB_MTX_LOCK(&sc->sc_mtx);
	usbd_transfer_start(sc->sc_xfer[UHUB_INTR_TRANSFER]);
	USB_MTX_UNLOCK(&sc->sc_mtx);

	/* Enable automatic power save on all USB HUBs */

	usbd_set_power_mode(udev, USB_POWER_MODE_SAVE);

	return (0);

error:
	usbd_transfer_unsetup(sc->sc_xfer, UHUB_N_TRANSFER);

#if (USB_HAVE_FIXED_PORT == 0)
	free(udev->hub, M_USBDEV);
#endif
	udev->hub = NULL;

	mtx_destroy(&sc->sc_mtx);

	return (ENXIO);
}

/*
 * Called from process context when the hub is gone.
 * Detach all devices on active ports.
 */
static int
uhub_detach(device_t dev)
{
	struct uhub_softc *sc = device_get_softc(dev);
	struct usb_hub *hub = sc->sc_udev->hub;
	struct usb_bus *bus = sc->sc_udev->bus;
	struct usb_device *child;
	uint8_t x;

	if (hub == NULL)		/* must be partially working */
		return (0);

	/* Make sure interrupt transfer is gone. */
	usbd_transfer_unsetup(sc->sc_xfer, UHUB_N_TRANSFER);

	/* Detach all ports */
	for (x = 0; x != hub->nports; x++) {

		child = usb_bus_port_get_device(bus, hub->ports + x);

		if (child == NULL) {
			continue;
		}

		/*
		 * Free USB device and all subdevices, if any.
		 */
		usb_free_device(child, 0);
	}

#if USB_HAVE_TT_SUPPORT
	/* Make sure our TT messages are not queued anywhere */
	USB_BUS_LOCK(bus);
	usb_proc_mwait(USB_BUS_TT_PROC(bus),
	    &hub->tt_msg[0], &hub->tt_msg[1]);
	USB_BUS_UNLOCK(bus);
#endif

#if (USB_HAVE_FIXED_PORT == 0)
	free(hub, M_USBDEV);
#endif
	sc->sc_udev->hub = NULL;

	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static int
uhub_suspend(device_t dev)
{
	DPRINTF("\n");
	/* Sub-devices are not suspended here! */
	return (0);
}

static int
uhub_resume(device_t dev)
{
	DPRINTF("\n");
	/* Sub-devices are not resumed here! */
	return (0);
}

static void
uhub_driver_added(device_t dev, driver_t *driver)
{
	usb_needs_explore_all();
}

struct hub_result {
	struct usb_device *udev;
	uint8_t	portno;
	uint8_t	iface_index;
};

static void
uhub_find_iface_index(struct usb_hub *hub, device_t child,
    struct hub_result *res)
{
	struct usb_interface *iface;
	struct usb_device *udev;
	uint8_t nports;
	uint8_t x;
	uint8_t i;

	nports = hub->nports;
	for (x = 0; x != nports; x++) {
		udev = usb_bus_port_get_device(hub->hubudev->bus,
		    hub->ports + x);
		if (!udev) {
			continue;
		}
		for (i = 0; i != USB_IFACE_MAX; i++) {
			iface = usbd_get_iface(udev, i);
			if (iface &&
			    (iface->subdev == child)) {
				res->iface_index = i;
				res->udev = udev;
				res->portno = x + 1;
				return;
			}
		}
	}
	res->iface_index = 0;
	res->udev = NULL;
	res->portno = 0;
}

static int
uhub_child_location_string(device_t parent, device_t child,
    char *buf, size_t buflen)
{
	struct uhub_softc *sc;
	struct usb_hub *hub;
	struct hub_result res;

	if (!device_is_attached(parent)) {
		if (buflen)
			buf[0] = 0;
		return (0);
	}

	sc = device_get_softc(parent);
	hub = sc->sc_udev->hub;

	mtx_lock(&Giant);
	uhub_find_iface_index(hub, child, &res);
	if (!res.udev) {
		DPRINTF("device not on hub\n");
		if (buflen) {
			buf[0] = '\0';
		}
		goto done;
	}
	snprintf(buf, buflen, "bus=%u hubaddr=%u port=%u devaddr=%u"
	    " interface=%u"
#if USB_HAVE_UGEN
	    " ugen=%s"
#endif
	    , device_get_unit(res.udev->bus->bdev)
	    , (res.udev->parent_hub != NULL) ?
	    res.udev->parent_hub->device_index : 0
	    , res.portno, res.udev->device_index, res.iface_index
#if USB_HAVE_UGEN
	    , res.udev->ugen_name
#endif
	    );
done:
	mtx_unlock(&Giant);

	return (0);
}

static int
uhub_child_pnpinfo_string(device_t parent, device_t child,
    char *buf, size_t buflen)
{
	struct uhub_softc *sc;
	struct usb_hub *hub;
	struct usb_interface *iface;
	struct hub_result res;

	if (!device_is_attached(parent)) {
		if (buflen)
			buf[0] = 0;
		return (0);
	}

	sc = device_get_softc(parent);
	hub = sc->sc_udev->hub;

	mtx_lock(&Giant);
	uhub_find_iface_index(hub, child, &res);
	if (!res.udev) {
		DPRINTF("device not on hub\n");
		if (buflen) {
			buf[0] = '\0';
		}
		goto done;
	}
	iface = usbd_get_iface(res.udev, res.iface_index);
	if (iface && iface->idesc) {
		snprintf(buf, buflen, "vendor=0x%04x product=0x%04x "
		    "devclass=0x%02x devsubclass=0x%02x "
		    "devproto=0x%02x "
		    "sernum=\"%s\" "
		    "release=0x%04x "
		    "mode=%s "
		    "intclass=0x%02x intsubclass=0x%02x "
		    "intprotocol=0x%02x" "%s%s",
		    UGETW(res.udev->ddesc.idVendor),
		    UGETW(res.udev->ddesc.idProduct),
		    res.udev->ddesc.bDeviceClass,
		    res.udev->ddesc.bDeviceSubClass,
		    res.udev->ddesc.bDeviceProtocol,
		    usb_get_serial(res.udev),
		    UGETW(res.udev->ddesc.bcdDevice),
		    (res.udev->flags.usb_mode == USB_MODE_HOST) ? "host" : "device",
		    iface->idesc->bInterfaceClass,
		    iface->idesc->bInterfaceSubClass,
		    iface->idesc->bInterfaceProtocol,
		    iface->pnpinfo ? " " : "",
		    iface->pnpinfo ? iface->pnpinfo : "");
	} else {
		if (buflen) {
			buf[0] = '\0';
		}
		goto done;
	}
done:
	mtx_unlock(&Giant);

	return (0);
}

/*
 * The USB Transaction Translator:
 * ===============================
 *
 * When doing LOW- and FULL-speed USB transfers across a HIGH-speed
 * USB HUB, bandwidth must be allocated for ISOCHRONOUS and INTERRUPT
 * USB transfers. To utilize bandwidth dynamically the "scatter and
 * gather" principle must be applied. This means that bandwidth must
 * be divided into equal parts of bandwidth. With regard to USB all
 * data is transferred in smaller packets with length
 * "wMaxPacketSize". The problem however is that "wMaxPacketSize" is
 * not a constant!
 *
 * The bandwidth scheduler which I have implemented will simply pack
 * the USB transfers back to back until there is no more space in the
 * schedule. Out of the 8 microframes which the USB 2.0 standard
 * provides, only 6 are available for non-HIGH-speed devices. I have
 * reserved the first 4 microframes for ISOCHRONOUS transfers. The
 * last 2 microframes I have reserved for INTERRUPT transfers. Without
 * this division, it is very difficult to allocate and free bandwidth
 * dynamically.
 *
 * NOTE about the Transaction Translator in USB HUBs:
 *
 * USB HUBs have a very simple Transaction Translator, that will
 * simply pipeline all the SPLIT transactions. That means that the
 * transactions will be executed in the order they are queued!
 *
 */

/*------------------------------------------------------------------------*
 *	usb_intr_find_best_slot
 *
 * Return value:
 *   The best Transaction Translation slot for an interrupt endpoint.
 *------------------------------------------------------------------------*/
static uint8_t
usb_intr_find_best_slot(usb_size_t *ptr, uint8_t start,
    uint8_t end, uint8_t mask)
{
	usb_size_t min = (usb_size_t)-1;
	usb_size_t sum;
	uint8_t x;
	uint8_t y;
	uint8_t z;

	y = 0;

	/* find the last slot with lesser used bandwidth */

	for (x = start; x < end; x++) {

		sum = 0;

		/* compute sum of bandwidth */
		for (z = x; z < end; z++) {
			if (mask & (1U << (z - x)))
				sum += ptr[z];
		}

		/* check if the current multi-slot is more optimal */
		if (min >= sum) {
			min = sum;
			y = x;
		}

		/* check if the mask is about to be shifted out */
		if (mask & (1U << (end - 1 - x)))
			break;
	}
	return (y);
}

/*------------------------------------------------------------------------*
 *	usb_hs_bandwidth_adjust
 *
 * This function will update the bandwidth usage for the microframe
 * having index "slot" by "len" bytes. "len" can be negative.  If the
 * "slot" argument is greater or equal to "USB_HS_MICRO_FRAMES_MAX"
 * the "slot" argument will be replaced by the slot having least used
 * bandwidth. The "mask" argument is used for multi-slot allocations.
 *
 * Returns:
 *    The slot in which the bandwidth update was done: 0..7
 *------------------------------------------------------------------------*/
static uint8_t
usb_hs_bandwidth_adjust(struct usb_device *udev, int16_t len,
    uint8_t slot, uint8_t mask)
{
	struct usb_bus *bus = udev->bus;
	struct usb_hub *hub;
	enum usb_dev_speed speed;
	uint8_t x;

	USB_BUS_LOCK_ASSERT(bus, MA_OWNED);

	speed = usbd_get_speed(udev);

	switch (speed) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
		if (speed == USB_SPEED_LOW) {
			len *= 8;
		}
		/*
	         * The Host Controller Driver should have
	         * performed checks so that the lookup
	         * below does not result in a NULL pointer
	         * access.
	         */

		hub = udev->parent_hs_hub->hub;
		if (slot >= USB_HS_MICRO_FRAMES_MAX) {
			slot = usb_intr_find_best_slot(hub->uframe_usage,
			    USB_FS_ISOC_UFRAME_MAX, 6, mask);
		}
		for (x = slot; x < 8; x++) {
			if (mask & (1U << (x - slot))) {
				hub->uframe_usage[x] += len;
				bus->uframe_usage[x] += len;
			}
		}
		break;
	default:
		if (slot >= USB_HS_MICRO_FRAMES_MAX) {
			slot = usb_intr_find_best_slot(bus->uframe_usage, 0,
			    USB_HS_MICRO_FRAMES_MAX, mask);
		}
		for (x = slot; x < 8; x++) {
			if (mask & (1U << (x - slot))) {
				bus->uframe_usage[x] += len;
			}
		}
		break;
	}
	return (slot);
}

/*------------------------------------------------------------------------*
 *	usb_hs_bandwidth_alloc
 *
 * This function is a wrapper function for "usb_hs_bandwidth_adjust()".
 *------------------------------------------------------------------------*/
void
usb_hs_bandwidth_alloc(struct usb_xfer *xfer)
{
	struct usb_device *udev;
	uint8_t slot;
	uint8_t mask;
	uint8_t speed;

	udev = xfer->xroot->udev;

	if (udev->flags.usb_mode != USB_MODE_HOST)
		return;		/* not supported */

	xfer->endpoint->refcount_bw++;
	if (xfer->endpoint->refcount_bw != 1)
		return;		/* already allocated */

	speed = usbd_get_speed(udev);

	switch (xfer->endpoint->edesc->bmAttributes & UE_XFERTYPE) {
	case UE_INTERRUPT:
		/* allocate a microframe slot */

		mask = 0x01;
		slot = usb_hs_bandwidth_adjust(udev,
		    xfer->max_frame_size, USB_HS_MICRO_FRAMES_MAX, mask);

		xfer->endpoint->usb_uframe = slot;
		xfer->endpoint->usb_smask = mask << slot;

		if ((speed != USB_SPEED_FULL) &&
		    (speed != USB_SPEED_LOW)) {
			xfer->endpoint->usb_cmask = 0x00 ;
		} else {
			xfer->endpoint->usb_cmask = (-(0x04 << slot)) & 0xFE;
		}
		break;

	case UE_ISOCHRONOUS:
		switch (usbd_xfer_get_fps_shift(xfer)) {
		case 0:
			mask = 0xFF;
			break;
		case 1:
			mask = 0x55;
			break;
		case 2:
			mask = 0x11;
			break;
		default:
			mask = 0x01;
			break;
		}

		/* allocate a microframe multi-slot */

		slot = usb_hs_bandwidth_adjust(udev,
		    xfer->max_frame_size, USB_HS_MICRO_FRAMES_MAX, mask);

		xfer->endpoint->usb_uframe = slot;
		xfer->endpoint->usb_cmask = 0;
		xfer->endpoint->usb_smask = mask << slot;
		break;

	default:
		xfer->endpoint->usb_uframe = 0;
		xfer->endpoint->usb_cmask = 0;
		xfer->endpoint->usb_smask = 0;
		break;
	}

	DPRINTFN(11, "slot=%d, mask=0x%02x\n", 
	    xfer->endpoint->usb_uframe, 
	    xfer->endpoint->usb_smask >> xfer->endpoint->usb_uframe);
}

/*------------------------------------------------------------------------*
 *	usb_hs_bandwidth_free
 *
 * This function is a wrapper function for "usb_hs_bandwidth_adjust()".
 *------------------------------------------------------------------------*/
void
usb_hs_bandwidth_free(struct usb_xfer *xfer)
{
	struct usb_device *udev;
	uint8_t slot;
	uint8_t mask;

	udev = xfer->xroot->udev;

	if (udev->flags.usb_mode != USB_MODE_HOST)
		return;		/* not supported */

	xfer->endpoint->refcount_bw--;
	if (xfer->endpoint->refcount_bw != 0)
		return;		/* still allocated */

	switch (xfer->endpoint->edesc->bmAttributes & UE_XFERTYPE) {
	case UE_INTERRUPT:
	case UE_ISOCHRONOUS:

		slot = xfer->endpoint->usb_uframe;
		mask = xfer->endpoint->usb_smask;

		/* free microframe slot(s): */ 	  
		usb_hs_bandwidth_adjust(udev,
		    -xfer->max_frame_size, slot, mask >> slot);

		DPRINTFN(11, "slot=%d, mask=0x%02x\n", 
		    slot, mask >> slot);

		xfer->endpoint->usb_uframe = 0;
		xfer->endpoint->usb_cmask = 0;
		xfer->endpoint->usb_smask = 0;
		break;

	default:
		break;
	}
}

/*------------------------------------------------------------------------*
 *	usb_isoc_time_expand
 *
 * This function will expand the time counter from 7-bit to 16-bit.
 *
 * Returns:
 *   16-bit isochronous time counter.
 *------------------------------------------------------------------------*/
uint16_t
usb_isoc_time_expand(struct usb_bus *bus, uint16_t isoc_time_curr)
{
	uint16_t rem;

	USB_BUS_LOCK_ASSERT(bus, MA_OWNED);

	rem = bus->isoc_time_last & (USB_ISOC_TIME_MAX - 1);

	isoc_time_curr &= (USB_ISOC_TIME_MAX - 1);

	if (isoc_time_curr < rem) {
		/* the time counter wrapped around */
		bus->isoc_time_last += USB_ISOC_TIME_MAX;
	}
	/* update the remainder */

	bus->isoc_time_last &= ~(USB_ISOC_TIME_MAX - 1);
	bus->isoc_time_last |= isoc_time_curr;

	return (bus->isoc_time_last);
}

/*------------------------------------------------------------------------*
 *	usbd_fs_isoc_schedule_alloc_slot
 *
 * This function will allocate bandwidth for an isochronous FULL speed
 * transaction in the FULL speed schedule.
 *
 * Returns:
 *    <8: Success
 * Else: Error
 *------------------------------------------------------------------------*/
#if USB_HAVE_TT_SUPPORT
uint8_t
usbd_fs_isoc_schedule_alloc_slot(struct usb_xfer *isoc_xfer, uint16_t isoc_time)
{
	struct usb_xfer *xfer;
	struct usb_xfer *pipe_xfer;
	struct usb_bus *bus;
	usb_frlength_t len;
	usb_frlength_t data_len;
	uint16_t delta;
	uint16_t slot;
	uint8_t retval;

	data_len = 0;
	slot = 0;

	bus = isoc_xfer->xroot->bus;

	TAILQ_FOREACH(xfer, &bus->intr_q.head, wait_entry) {

		/* skip self, if any */

		if (xfer == isoc_xfer)
			continue;

		/* check if this USB transfer is going through the same TT */

		if (xfer->xroot->udev->parent_hs_hub !=
		    isoc_xfer->xroot->udev->parent_hs_hub) {
			continue;
		}
		if ((isoc_xfer->xroot->udev->parent_hs_hub->
		    ddesc.bDeviceProtocol == UDPROTO_HSHUBMTT) &&
		    (xfer->xroot->udev->hs_port_no !=
		    isoc_xfer->xroot->udev->hs_port_no)) {
			continue;
		}
		if (xfer->endpoint->methods != isoc_xfer->endpoint->methods)
			continue;

		/* check if isoc_time is part of this transfer */

		delta = xfer->isoc_time_complete - isoc_time;
		if (delta > 0 && delta <= xfer->nframes) {
			delta = xfer->nframes - delta;

			len = xfer->frlengths[delta];
			len += 8;
			len *= 7;
			len /= 6;

			data_len += len;
		}

		/*
		 * Check double buffered transfers. Only stream ID
		 * equal to zero is valid here!
		 */
		TAILQ_FOREACH(pipe_xfer, &xfer->endpoint->endpoint_q[0].head,
		    wait_entry) {

			/* skip self, if any */

			if (pipe_xfer == isoc_xfer)
				continue;

			/* check if isoc_time is part of this transfer */

			delta = pipe_xfer->isoc_time_complete - isoc_time;
			if (delta > 0 && delta <= pipe_xfer->nframes) {
				delta = pipe_xfer->nframes - delta;

				len = pipe_xfer->frlengths[delta];
				len += 8;
				len *= 7;
				len /= 6;

				data_len += len;
			}
		}
	}

	while (data_len >= USB_FS_BYTES_PER_HS_UFRAME) {
		data_len -= USB_FS_BYTES_PER_HS_UFRAME;
		slot++;
	}

	/* check for overflow */

	if (slot >= USB_FS_ISOC_UFRAME_MAX)
		return (255);

	retval = slot;

	delta = isoc_xfer->isoc_time_complete - isoc_time;
	if (delta > 0 && delta <= isoc_xfer->nframes) {
		delta = isoc_xfer->nframes - delta;

		len = isoc_xfer->frlengths[delta];
		len += 8;
		len *= 7;
		len /= 6;

		data_len += len;
	}

	while (data_len >= USB_FS_BYTES_PER_HS_UFRAME) {
		data_len -= USB_FS_BYTES_PER_HS_UFRAME;
		slot++;
	}

	/* check for overflow */

	if (slot >= USB_FS_ISOC_UFRAME_MAX)
		return (255);

	return (retval);
}
#endif

/*------------------------------------------------------------------------*
 *	usb_bus_port_get_device
 *
 * This function is NULL safe.
 *------------------------------------------------------------------------*/
struct usb_device *
usb_bus_port_get_device(struct usb_bus *bus, struct usb_port *up)
{
	if ((bus == NULL) || (up == NULL)) {
		/* be NULL safe */
		return (NULL);
	}
	if (up->device_index == 0) {
		/* nothing to do */
		return (NULL);
	}
	return (bus->devices[up->device_index]);
}

/*------------------------------------------------------------------------*
 *	usb_bus_port_set_device
 *
 * This function is NULL safe.
 *------------------------------------------------------------------------*/
void
usb_bus_port_set_device(struct usb_bus *bus, struct usb_port *up,
    struct usb_device *udev, uint8_t device_index)
{
	if (bus == NULL) {
		/* be NULL safe */
		return;
	}
	/*
	 * There is only one case where we don't
	 * have an USB port, and that is the Root Hub!
         */
	if (up) {
		if (udev) {
			up->device_index = device_index;
		} else {
			device_index = up->device_index;
			up->device_index = 0;
		}
	}
	/*
	 * Make relationships to our new device
	 */
	if (device_index != 0) {
#if USB_HAVE_UGEN
		mtx_lock(&usb_ref_lock);
#endif
		bus->devices[device_index] = udev;
#if USB_HAVE_UGEN
		mtx_unlock(&usb_ref_lock);
#endif
	}
	/*
	 * Debug print
	 */
	DPRINTFN(2, "bus %p devices[%u] = %p\n", bus, device_index, udev);
}

/*------------------------------------------------------------------------*
 *	usb_needs_explore
 *
 * This functions is called when the USB event thread needs to run.
 *------------------------------------------------------------------------*/
void
usb_needs_explore(struct usb_bus *bus, uint8_t do_probe)
{
	uint8_t do_unlock;

	DPRINTF("\n");

	if (cold != 0) {
		DPRINTF("Cold\n");
		return;
	}

	if (bus == NULL) {
		DPRINTF("No bus pointer!\n");
		return;
	}
	if ((bus->devices == NULL) ||
	    (bus->devices[USB_ROOT_HUB_ADDR] == NULL)) {
		DPRINTF("No root HUB\n");
		return;
	}
	if (mtx_owned(&bus->bus_mtx)) {
		do_unlock = 0;
	} else {
		USB_BUS_LOCK(bus);
		do_unlock = 1;
	}
	if (do_probe) {
		bus->do_probe = 1;
	}
	if (usb_proc_msignal(USB_BUS_EXPLORE_PROC(bus),
	    &bus->explore_msg[0], &bus->explore_msg[1])) {
		/* ignore */
	}
	if (do_unlock) {
		USB_BUS_UNLOCK(bus);
	}
}

/*------------------------------------------------------------------------*
 *	usb_needs_explore_all
 *
 * This function is called whenever a new driver is loaded and will
 * cause that all USB buses are re-explored.
 *------------------------------------------------------------------------*/
void
usb_needs_explore_all(void)
{
	struct usb_bus *bus;
	devclass_t dc;
	device_t dev;
	int max;

	DPRINTFN(3, "\n");

	dc = usb_devclass_ptr;
	if (dc == NULL) {
		DPRINTFN(0, "no devclass\n");
		return;
	}
	/*
	 * Explore all USB buses in parallel.
	 */
	max = devclass_get_maxunit(dc);
	while (max >= 0) {
		dev = devclass_get_device(dc, max);
		if (dev) {
			bus = device_get_softc(dev);
			if (bus) {
				usb_needs_explore(bus, 1);
			}
		}
		max--;
	}
}

/*------------------------------------------------------------------------*
 *	usb_needs_explore_init
 *
 * This function will ensure that the USB controllers are not enumerated
 * until the "cold" variable is cleared.
 *------------------------------------------------------------------------*/
static void
usb_needs_explore_init(void *arg)
{
	/*
	 * The cold variable should be cleared prior to this function
	 * being called:
	 */
	if (cold == 0)
		usb_needs_explore_all();
	else
		DPRINTFN(-1, "Cold variable is still set!\n");
}
SYSINIT(usb_needs_explore_init, SI_SUB_KICK_SCHEDULER, SI_ORDER_SECOND, usb_needs_explore_init, NULL);

/*------------------------------------------------------------------------*
 *	usb_bus_power_update
 *
 * This function will ensure that all USB devices on the given bus are
 * properly suspended or resumed according to the device transfer
 * state.
 *------------------------------------------------------------------------*/
#if USB_HAVE_POWERD
void
usb_bus_power_update(struct usb_bus *bus)
{
	usb_needs_explore(bus, 0 /* no probe */ );
}
#endif

/*------------------------------------------------------------------------*
 *	usbd_transfer_power_ref
 *
 * This function will modify the power save reference counts and
 * wakeup the USB device associated with the given USB transfer, if
 * needed.
 *------------------------------------------------------------------------*/
#if USB_HAVE_POWERD
void
usbd_transfer_power_ref(struct usb_xfer *xfer, int val)
{
	static const usb_power_mask_t power_mask[4] = {
		[UE_CONTROL] = USB_HW_POWER_CONTROL,
		[UE_BULK] = USB_HW_POWER_BULK,
		[UE_INTERRUPT] = USB_HW_POWER_INTERRUPT,
		[UE_ISOCHRONOUS] = USB_HW_POWER_ISOC,
	};
	struct usb_device *udev;
	uint8_t needs_explore;
	uint8_t needs_hw_power;
	uint8_t xfer_type;

	udev = xfer->xroot->udev;

	if (udev->device_index == USB_ROOT_HUB_ADDR) {
		/* no power save for root HUB */
		return;
	}
	USB_BUS_LOCK(udev->bus);

	xfer_type = xfer->endpoint->edesc->bmAttributes & UE_XFERTYPE;

	udev->pwr_save.last_xfer_time = ticks;
	udev->pwr_save.type_refs[xfer_type] += val;

	if (xfer->flags_int.control_xfr) {
		udev->pwr_save.read_refs += val;
		if (xfer->flags_int.usb_mode == USB_MODE_HOST) {
			/*
			 * It is not allowed to suspend during a
			 * control transfer:
			 */
			udev->pwr_save.write_refs += val;
		}
	} else if (USB_GET_DATA_ISREAD(xfer)) {
		udev->pwr_save.read_refs += val;
	} else {
		udev->pwr_save.write_refs += val;
	}

	if (val > 0) {
		if (udev->flags.self_suspended)
			needs_explore = usb_peer_should_wakeup(udev);
		else
			needs_explore = 0;

		if (!(udev->bus->hw_power_state & power_mask[xfer_type])) {
			DPRINTF("Adding type %u to power state\n", xfer_type);
			udev->bus->hw_power_state |= power_mask[xfer_type];
			needs_hw_power = 1;
		} else {
			needs_hw_power = 0;
		}
	} else {
		needs_explore = 0;
		needs_hw_power = 0;
	}

	USB_BUS_UNLOCK(udev->bus);

	if (needs_explore) {
		DPRINTF("update\n");
		usb_bus_power_update(udev->bus);
	} else if (needs_hw_power) {
		DPRINTF("needs power\n");
		if (udev->bus->methods->set_hw_power != NULL) {
			(udev->bus->methods->set_hw_power) (udev->bus);
		}
	}
}
#endif

/*------------------------------------------------------------------------*
 *	usb_peer_should_wakeup
 *
 * This function returns non-zero if the current device should wake up.
 *------------------------------------------------------------------------*/
static uint8_t
usb_peer_should_wakeup(struct usb_device *udev)
{
	return (((udev->power_mode == USB_POWER_MODE_ON) &&
	    (udev->flags.usb_mode == USB_MODE_HOST)) ||
	    (udev->driver_added_refcount != udev->bus->driver_added_refcount) ||
	    (udev->re_enumerate_wait != USB_RE_ENUM_DONE) ||
	    (udev->pwr_save.type_refs[UE_ISOCHRONOUS] != 0) ||
	    (udev->pwr_save.write_refs != 0) ||
	    ((udev->pwr_save.read_refs != 0) &&
	    (udev->flags.usb_mode == USB_MODE_HOST) &&
	    (usb_peer_can_wakeup(udev) == 0)));
}

/*------------------------------------------------------------------------*
 *	usb_bus_powerd
 *
 * This function implements the USB power daemon and is called
 * regularly from the USB explore thread.
 *------------------------------------------------------------------------*/
#if USB_HAVE_POWERD
void
usb_bus_powerd(struct usb_bus *bus)
{
	struct usb_device *udev;
	usb_ticks_t temp;
	usb_ticks_t limit;
	usb_ticks_t mintime;
	usb_size_t type_refs[5];
	uint8_t x;

	limit = usb_power_timeout;
	if (limit == 0)
		limit = hz;
	else if (limit > 255)
		limit = 255 * hz;
	else
		limit = limit * hz;

	DPRINTF("bus=%p\n", bus);

	USB_BUS_LOCK(bus);

	/*
	 * The root HUB device is never suspended
	 * and we simply skip it.
	 */
	for (x = USB_ROOT_HUB_ADDR + 1;
	    x != bus->devices_max; x++) {

		udev = bus->devices[x];
		if (udev == NULL)
			continue;

		temp = ticks - udev->pwr_save.last_xfer_time;

		if (usb_peer_should_wakeup(udev)) {
			/* check if we are suspended */
			if (udev->flags.self_suspended != 0) {
				USB_BUS_UNLOCK(bus);
				usb_dev_resume_peer(udev);
				USB_BUS_LOCK(bus);
			}
		} else if ((temp >= limit) &&
		    (udev->flags.usb_mode == USB_MODE_HOST) &&
		    (udev->flags.self_suspended == 0)) {
			/* try to do suspend */

			USB_BUS_UNLOCK(bus);
			usb_dev_suspend_peer(udev);
			USB_BUS_LOCK(bus);
		}
	}

	/* reset counters */

	mintime = (usb_ticks_t)-1;
	type_refs[0] = 0;
	type_refs[1] = 0;
	type_refs[2] = 0;
	type_refs[3] = 0;
	type_refs[4] = 0;

	/* Re-loop all the devices to get the actual state */

	for (x = USB_ROOT_HUB_ADDR + 1;
	    x != bus->devices_max; x++) {

		udev = bus->devices[x];
		if (udev == NULL)
			continue;

		/* we found a non-Root-Hub USB device */
		type_refs[4] += 1;

		/* "last_xfer_time" can be updated by a resume */
		temp = ticks - udev->pwr_save.last_xfer_time;

		/*
		 * Compute minimum time since last transfer for the complete
		 * bus:
		 */
		if (temp < mintime)
			mintime = temp;

		if (udev->flags.self_suspended == 0) {
			type_refs[0] += udev->pwr_save.type_refs[0];
			type_refs[1] += udev->pwr_save.type_refs[1];
			type_refs[2] += udev->pwr_save.type_refs[2];
			type_refs[3] += udev->pwr_save.type_refs[3];
		}
	}

	if (mintime >= (usb_ticks_t)(1 * hz)) {
		/* recompute power masks */
		DPRINTF("Recomputing power masks\n");
		bus->hw_power_state = 0;
		if (type_refs[UE_CONTROL] != 0)
			bus->hw_power_state |= USB_HW_POWER_CONTROL;
		if (type_refs[UE_BULK] != 0)
			bus->hw_power_state |= USB_HW_POWER_BULK;
		if (type_refs[UE_INTERRUPT] != 0)
			bus->hw_power_state |= USB_HW_POWER_INTERRUPT;
		if (type_refs[UE_ISOCHRONOUS] != 0)
			bus->hw_power_state |= USB_HW_POWER_ISOC;
		if (type_refs[4] != 0)
			bus->hw_power_state |= USB_HW_POWER_NON_ROOT_HUB;
	}
	USB_BUS_UNLOCK(bus);

	if (bus->methods->set_hw_power != NULL) {
		/* always update hardware power! */
		(bus->methods->set_hw_power) (bus);
	}
	return;
}
#endif

/*------------------------------------------------------------------------*
 *	usb_dev_resume_peer
 *
 * This function will resume an USB peer and do the required USB
 * signalling to get an USB device out of the suspended state.
 *------------------------------------------------------------------------*/
static void
usb_dev_resume_peer(struct usb_device *udev)
{
	struct usb_bus *bus;
	int err;

	/* be NULL safe */
	if (udev == NULL)
		return;

	/* check if already resumed */
	if (udev->flags.self_suspended == 0)
		return;

	/* we need a parent HUB to do resume */
	if (udev->parent_hub == NULL)
		return;

	DPRINTF("udev=%p\n", udev);

	if ((udev->flags.usb_mode == USB_MODE_DEVICE) &&
	    (udev->flags.remote_wakeup == 0)) {
		/*
		 * If the host did not set the remote wakeup feature, we can
		 * not wake it up either!
		 */
		DPRINTF("remote wakeup is not set!\n");
		return;
	}
	/* get bus pointer */
	bus = udev->bus;

	/* resume parent hub first */
	usb_dev_resume_peer(udev->parent_hub);

	/* reduce chance of instant resume failure by waiting a little bit */
	usb_pause_mtx(NULL, USB_MS_TO_TICKS(20));

	if (usb_device_20_compatible(udev)) {
		/* resume current port (Valid in Host and Device Mode) */
		err = usbd_req_clear_port_feature(udev->parent_hub,
		    NULL, udev->port_no, UHF_PORT_SUSPEND);
		if (err) {
			DPRINTFN(0, "Resuming port failed\n");
			return;
		}
	} else {
		/* resume current port (Valid in Host and Device Mode) */
		err = usbd_req_set_port_link_state(udev->parent_hub,
		    NULL, udev->port_no, UPS_PORT_LS_U0);
		if (err) {
			DPRINTFN(0, "Resuming port failed\n");
			return;
		}
	}

	/* resume settle time */
	usb_pause_mtx(NULL, USB_MS_TO_TICKS(usb_port_resume_delay));

	if (bus->methods->device_resume != NULL) {
		/* resume USB device on the USB controller */
		(bus->methods->device_resume) (udev);
	}
	USB_BUS_LOCK(bus);
	/* set that this device is now resumed */
	udev->flags.self_suspended = 0;
#if USB_HAVE_POWERD
	/* make sure that we don't go into suspend right away */
	udev->pwr_save.last_xfer_time = ticks;

	/* make sure the needed power masks are on */
	if (udev->pwr_save.type_refs[UE_CONTROL] != 0)
		bus->hw_power_state |= USB_HW_POWER_CONTROL;
	if (udev->pwr_save.type_refs[UE_BULK] != 0)
		bus->hw_power_state |= USB_HW_POWER_BULK;
	if (udev->pwr_save.type_refs[UE_INTERRUPT] != 0)
		bus->hw_power_state |= USB_HW_POWER_INTERRUPT;
	if (udev->pwr_save.type_refs[UE_ISOCHRONOUS] != 0)
		bus->hw_power_state |= USB_HW_POWER_ISOC;
#endif
	USB_BUS_UNLOCK(bus);

	if (bus->methods->set_hw_power != NULL) {
		/* always update hardware power! */
		(bus->methods->set_hw_power) (bus);
	}

	usbd_sr_lock(udev);

	/* notify all sub-devices about resume */
	err = usb_suspend_resume(udev, 0);

	usbd_sr_unlock(udev);

	/* check if peer has wakeup capability */
	if (usb_peer_can_wakeup(udev)) {
		/* clear remote wakeup */
		err = usbd_req_clear_device_feature(udev,
		    NULL, UF_DEVICE_REMOTE_WAKEUP);
		if (err) {
			DPRINTFN(0, "Clearing device "
			    "remote wakeup failed: %s\n",
			    usbd_errstr(err));
		}
	}
}

/*------------------------------------------------------------------------*
 *	usb_dev_suspend_peer
 *
 * This function will suspend an USB peer and do the required USB
 * signalling to get an USB device into the suspended state.
 *------------------------------------------------------------------------*/
static void
usb_dev_suspend_peer(struct usb_device *udev)
{
	struct usb_device *child;
	int err;
	uint8_t x;
	uint8_t nports;

repeat:
	/* be NULL safe */
	if (udev == NULL)
		return;

	/* check if already suspended */
	if (udev->flags.self_suspended)
		return;

	/* we need a parent HUB to do suspend */
	if (udev->parent_hub == NULL)
		return;

	DPRINTF("udev=%p\n", udev);

	/* check if the current device is a HUB */
	if (udev->hub != NULL) {
		nports = udev->hub->nports;

		/* check if all devices on the HUB are suspended */
		for (x = 0; x != nports; x++) {
			child = usb_bus_port_get_device(udev->bus,
			    udev->hub->ports + x);

			if (child == NULL)
				continue;

			if (child->flags.self_suspended)
				continue;

			DPRINTFN(1, "Port %u is busy on the HUB!\n", x + 1);
			return;
		}
	}

	if (usb_peer_can_wakeup(udev)) {
		/*
		 * This request needs to be done before we set
		 * "udev->flags.self_suspended":
		 */

		/* allow device to do remote wakeup */
		err = usbd_req_set_device_feature(udev,
		    NULL, UF_DEVICE_REMOTE_WAKEUP);
		if (err) {
			DPRINTFN(0, "Setting device "
			    "remote wakeup failed\n");
		}
	}

	USB_BUS_LOCK(udev->bus);
	/*
	 * Checking for suspend condition and setting suspended bit
	 * must be atomic!
	 */
	err = usb_peer_should_wakeup(udev);
	if (err == 0) {
		/*
		 * Set that this device is suspended. This variable
		 * must be set before calling USB controller suspend
		 * callbacks.
		 */
		udev->flags.self_suspended = 1;
	}
	USB_BUS_UNLOCK(udev->bus);

	if (err != 0) {
		if (usb_peer_can_wakeup(udev)) {
			/* allow device to do remote wakeup */
			err = usbd_req_clear_device_feature(udev,
			    NULL, UF_DEVICE_REMOTE_WAKEUP);
			if (err) {
				DPRINTFN(0, "Setting device "
				    "remote wakeup failed\n");
			}
		}

		if (udev->flags.usb_mode == USB_MODE_DEVICE) {
			/* resume parent HUB first */
			usb_dev_resume_peer(udev->parent_hub);

			/* reduce chance of instant resume failure by waiting a little bit */
			usb_pause_mtx(NULL, USB_MS_TO_TICKS(20));

			/* resume current port (Valid in Host and Device Mode) */
			err = usbd_req_clear_port_feature(udev->parent_hub,
			    NULL, udev->port_no, UHF_PORT_SUSPEND);

			/* resume settle time */
			usb_pause_mtx(NULL, USB_MS_TO_TICKS(usb_port_resume_delay));
		}
		DPRINTF("Suspend was cancelled!\n");
		return;
	}

	usbd_sr_lock(udev);

	/* notify all sub-devices about suspend */
	err = usb_suspend_resume(udev, 1);

	usbd_sr_unlock(udev);

	if (udev->bus->methods->device_suspend != NULL) {
		usb_timeout_t temp;

		/* suspend device on the USB controller */
		(udev->bus->methods->device_suspend) (udev);

		/* do DMA delay */
		temp = usbd_get_dma_delay(udev);
		if (temp != 0)
			usb_pause_mtx(NULL, USB_MS_TO_TICKS(temp));

	}

	if (usb_device_20_compatible(udev)) {
		/* suspend current port */
		err = usbd_req_set_port_feature(udev->parent_hub,
		    NULL, udev->port_no, UHF_PORT_SUSPEND);
		if (err) {
			DPRINTFN(0, "Suspending port failed\n");
			return;
		}
	} else {
		/* suspend current port */
		err = usbd_req_set_port_link_state(udev->parent_hub,
		    NULL, udev->port_no, UPS_PORT_LS_U3);
		if (err) {
			DPRINTFN(0, "Suspending port failed\n");
			return;
		}
	}

	udev = udev->parent_hub;
	goto repeat;
}

/*------------------------------------------------------------------------*
 *	usbd_set_power_mode
 *
 * This function will set the power mode, see USB_POWER_MODE_XXX for a
 * USB device.
 *------------------------------------------------------------------------*/
void
usbd_set_power_mode(struct usb_device *udev, uint8_t power_mode)
{
	/* filter input argument */
	if ((power_mode != USB_POWER_MODE_ON) &&
	    (power_mode != USB_POWER_MODE_OFF))
		power_mode = USB_POWER_MODE_SAVE;

	power_mode = usbd_filter_power_mode(udev, power_mode);	

	udev->power_mode = power_mode;	/* update copy of power mode */

#if USB_HAVE_POWERD
	usb_bus_power_update(udev->bus);
#else
	usb_needs_explore(udev->bus, 0 /* no probe */ );
#endif
}

/*------------------------------------------------------------------------*
 *	usbd_filter_power_mode
 *
 * This function filters the power mode based on hardware requirements.
 *------------------------------------------------------------------------*/
uint8_t
usbd_filter_power_mode(struct usb_device *udev, uint8_t power_mode)
{
	const struct usb_bus_methods *mtod;
	int8_t temp;

	mtod = udev->bus->methods;
	temp = -1;

	if (mtod->get_power_mode != NULL)
		(mtod->get_power_mode) (udev, &temp);

	/* check if we should not filter */
	if (temp < 0)
		return (power_mode);

	/* use fixed power mode given by hardware driver */
	return (temp);
}

/*------------------------------------------------------------------------*
 *	usbd_start_re_enumerate
 *
 * This function starts re-enumeration of the given USB device. This
 * function does not need to be called BUS-locked. This function does
 * not wait until the re-enumeration is completed.
 *------------------------------------------------------------------------*/
void
usbd_start_re_enumerate(struct usb_device *udev)
{
	if (udev->re_enumerate_wait == USB_RE_ENUM_DONE) {
		udev->re_enumerate_wait = USB_RE_ENUM_START;
		usb_needs_explore(udev->bus, 0);
	}
}

/*-----------------------------------------------------------------------*
 *	usbd_start_set_config
 *
 * This function starts setting a USB configuration. This function
 * does not need to be called BUS-locked. This function does not wait
 * until the set USB configuratino is completed.
 *------------------------------------------------------------------------*/
usb_error_t
usbd_start_set_config(struct usb_device *udev, uint8_t index)
{
	if (udev->re_enumerate_wait == USB_RE_ENUM_DONE) {
		if (udev->curr_config_index == index) {
			/* no change needed */
			return (0);
		}
		udev->next_config_index = index;
		udev->re_enumerate_wait = USB_RE_ENUM_SET_CONFIG;
		usb_needs_explore(udev->bus, 0);
		return (0);
	} else if (udev->re_enumerate_wait == USB_RE_ENUM_SET_CONFIG) {
		if (udev->next_config_index == index) {
			/* no change needed */
			return (0);
		}
	}
	return (USB_ERR_PENDING_REQUESTS);
}
