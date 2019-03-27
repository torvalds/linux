/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
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
#include "usb_if.h"

#define	USB_DEBUG_VAR usb_debug

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_transfer.h>
#include <dev/usb/usb_device.h>
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_dynamic.h>
#include <dev/usb/usb_hub.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#endif			/* USB_GLOBAL_INCLUDE_FILE */

/* function prototypes */

static uint8_t usb_handle_get_stall(struct usb_device *, uint8_t);
static usb_error_t	 usb_handle_remote_wakeup(struct usb_xfer *, uint8_t);
static usb_error_t	 usb_handle_request(struct usb_xfer *);
static usb_error_t	 usb_handle_set_config(struct usb_xfer *, uint8_t);
static usb_error_t	 usb_handle_set_stall(struct usb_xfer *, uint8_t,
			    uint8_t);
static usb_error_t	 usb_handle_iface_request(struct usb_xfer *, void **,
			    uint16_t *, struct usb_device_request, uint16_t,
			    uint8_t);

/*------------------------------------------------------------------------*
 *	usb_handle_request_callback
 *
 * This function is the USB callback for generic USB Device control
 * transfers.
 *------------------------------------------------------------------------*/
void
usb_handle_request_callback(struct usb_xfer *xfer, usb_error_t error)
{
	usb_error_t err;

	/* check the current transfer state */

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:

		/* handle the request */
		err = usb_handle_request(xfer);

		if (err) {

			if (err == USB_ERR_BAD_CONTEXT) {
				/* we need to re-setup the control transfer */
				usb_needs_explore(xfer->xroot->bus, 0);
				break;
			}
			goto tr_restart;
		}
		usbd_transfer_submit(xfer);
		break;

	default:
		/* check if a control transfer is active */
		if (xfer->flags_int.control_rem != 0xFFFF) {
			/* handle the request */
			err = usb_handle_request(xfer);
		}
		if (xfer->error != USB_ERR_CANCELLED) {
			/* should not happen - try stalling */
			goto tr_restart;
		}
		break;
	}
	return;

tr_restart:
	/*
	 * If a control transfer is active, stall it, and wait for the
	 * next control transfer.
	 */
	usbd_xfer_set_frame_len(xfer, 0, sizeof(struct usb_device_request));
	xfer->nframes = 1;
	xfer->flags.manual_status = 1;
	xfer->flags.force_short_xfer = 0;
	usbd_xfer_set_stall(xfer);	/* cancel previous transfer, if any */
	usbd_transfer_submit(xfer);
}

/*------------------------------------------------------------------------*
 *	usb_handle_set_config
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
static usb_error_t
usb_handle_set_config(struct usb_xfer *xfer, uint8_t conf_no)
{
	struct usb_device *udev = xfer->xroot->udev;
	usb_error_t err = 0;
	uint8_t do_unlock;

	/*
	 * We need to protect against other threads doing probe and
	 * attach:
	 */
	USB_XFER_UNLOCK(xfer);

	/* Prevent re-enumeration */
	do_unlock = usbd_enum_lock(udev);

	if (conf_no == USB_UNCONFIG_NO) {
		conf_no = USB_UNCONFIG_INDEX;
	} else {
		/*
		 * The relationship between config number and config index
		 * is very simple in our case:
		 */
		conf_no--;
	}

	if (usbd_set_config_index(udev, conf_no)) {
		DPRINTF("set config %d failed\n", conf_no);
		err = USB_ERR_STALLED;
		goto done;
	}
	if (usb_probe_and_attach(udev, USB_IFACE_INDEX_ANY)) {
		DPRINTF("probe and attach failed\n");
		err = USB_ERR_STALLED;
		goto done;
	}
done:
	if (do_unlock)
		usbd_enum_unlock(udev);
	USB_XFER_LOCK(xfer);
	return (err);
}

static usb_error_t
usb_check_alt_setting(struct usb_device *udev, 
     struct usb_interface *iface, uint8_t alt_index)
{
	uint8_t do_unlock;
	usb_error_t err = 0;

	/* Prevent re-enumeration */
	do_unlock = usbd_enum_lock(udev);

	if (alt_index >= usbd_get_no_alts(udev->cdesc, iface->idesc))
		err = USB_ERR_INVAL;

	if (do_unlock)
		usbd_enum_unlock(udev);

	return (err);
}

/*------------------------------------------------------------------------*
 *	usb_handle_iface_request
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
static usb_error_t
usb_handle_iface_request(struct usb_xfer *xfer,
    void **ppdata, uint16_t *plen,
    struct usb_device_request req, uint16_t off, uint8_t state)
{
	struct usb_interface *iface;
	struct usb_interface *iface_parent;	/* parent interface */
	struct usb_device *udev = xfer->xroot->udev;
	int error;
	uint8_t iface_index;
	uint8_t temp_state;
	uint8_t do_unlock;

	if ((req.bmRequestType & 0x1F) == UT_INTERFACE) {
		iface_index = req.wIndex[0];	/* unicast */
	} else {
		iface_index = 0;	/* broadcast */
	}

	/*
	 * We need to protect against other threads doing probe and
	 * attach:
	 */
	USB_XFER_UNLOCK(xfer);

	/* Prevent re-enumeration */
	do_unlock = usbd_enum_lock(udev);

	error = ENXIO;

tr_repeat:
	iface = usbd_get_iface(udev, iface_index);
	if ((iface == NULL) ||
	    (iface->idesc == NULL)) {
		/* end of interfaces non-existing interface */
		goto tr_stalled;
	}
	/* set initial state */

	temp_state = state;

	/* forward request to interface, if any */

	if ((error != 0) &&
	    (error != ENOTTY) &&
	    (iface->subdev != NULL) &&
	    device_is_attached(iface->subdev)) {
#if 0
		DEVMETHOD(usb_handle_request, NULL);	/* dummy */
#endif
		error = USB_HANDLE_REQUEST(iface->subdev,
		    &req, ppdata, plen,
		    off, &temp_state);
	}
	iface_parent = usbd_get_iface(udev, iface->parent_iface_index);

	if ((iface_parent == NULL) ||
	    (iface_parent->idesc == NULL)) {
		/* non-existing interface */
		iface_parent = NULL;
	}
	/* forward request to parent interface, if any */

	if ((error != 0) &&
	    (error != ENOTTY) &&
	    (iface_parent != NULL) &&
	    (iface_parent->subdev != NULL) &&
	    ((req.bmRequestType & 0x1F) == UT_INTERFACE) &&
	    (iface_parent->subdev != iface->subdev) &&
	    device_is_attached(iface_parent->subdev)) {
		error = USB_HANDLE_REQUEST(iface_parent->subdev,
		    &req, ppdata, plen, off, &temp_state);
	}
	if (error == 0) {
		/* negativly adjust pointer and length */
		*ppdata = ((uint8_t *)(*ppdata)) - off;
		*plen += off;

		if ((state == USB_HR_NOT_COMPLETE) &&
		    (temp_state == USB_HR_COMPLETE_OK))
			goto tr_short;
		else
			goto tr_valid;
	} else if (error == ENOTTY) {
		goto tr_stalled;
	}
	if ((req.bmRequestType & 0x1F) != UT_INTERFACE) {
		iface_index++;		/* iterate */
		goto tr_repeat;
	}
	if (state != USB_HR_NOT_COMPLETE) {
		/* we are complete */
		goto tr_valid;
	}
	switch (req.bmRequestType) {
	case UT_WRITE_INTERFACE:
		switch (req.bRequest) {
		case UR_SET_INTERFACE:
			/*
			 * We assume that the endpoints are the same
			 * across the alternate settings.
			 *
			 * Reset the endpoints, because re-attaching
			 * only a part of the device is not possible.
			 */
			error = usb_check_alt_setting(udev,
			    iface, req.wValue[0]);
			if (error) {
				DPRINTF("alt setting does not exist %s\n",
				    usbd_errstr(error));
				goto tr_stalled;
			}
			error = usb_reset_iface_endpoints(udev, iface_index);
			if (error) {
				DPRINTF("alt setting failed %s\n",
				    usbd_errstr(error));
				goto tr_stalled;
			}
			/* update the current alternate setting */
			iface->alt_index = req.wValue[0];
			break;

		default:
			goto tr_stalled;
		}
		break;

	case UT_READ_INTERFACE:
		switch (req.bRequest) {
		case UR_GET_INTERFACE:
			*ppdata = &iface->alt_index;
			*plen = 1;
			break;

		default:
			goto tr_stalled;
		}
		break;
	default:
		goto tr_stalled;
	}
tr_valid:
	if (do_unlock)
		usbd_enum_unlock(udev);
	USB_XFER_LOCK(xfer);
	return (0);

tr_short:
	if (do_unlock)
		usbd_enum_unlock(udev);
	USB_XFER_LOCK(xfer);
	return (USB_ERR_SHORT_XFER);

tr_stalled:
	if (do_unlock)
		usbd_enum_unlock(udev);
	USB_XFER_LOCK(xfer);
	return (USB_ERR_STALLED);
}

/*------------------------------------------------------------------------*
 *	usb_handle_stall
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
static usb_error_t
usb_handle_set_stall(struct usb_xfer *xfer, uint8_t ep, uint8_t do_stall)
{
	struct usb_device *udev = xfer->xroot->udev;
	usb_error_t err;

	USB_XFER_UNLOCK(xfer);
	err = usbd_set_endpoint_stall(udev,
	    usbd_get_ep_by_addr(udev, ep), do_stall);
	USB_XFER_LOCK(xfer);
	return (err);
}

/*------------------------------------------------------------------------*
 *	usb_handle_get_stall
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
static uint8_t
usb_handle_get_stall(struct usb_device *udev, uint8_t ea_val)
{
	struct usb_endpoint *ep;
	uint8_t halted;

	ep = usbd_get_ep_by_addr(udev, ea_val);
	if (ep == NULL) {
		/* nothing to do */
		return (0);
	}
	USB_BUS_LOCK(udev->bus);
	halted = ep->is_stalled;
	USB_BUS_UNLOCK(udev->bus);

	return (halted);
}

/*------------------------------------------------------------------------*
 *	usb_handle_remote_wakeup
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
static usb_error_t
usb_handle_remote_wakeup(struct usb_xfer *xfer, uint8_t is_on)
{
	struct usb_device *udev;
	struct usb_bus *bus;

	udev = xfer->xroot->udev;
	bus = udev->bus;

	USB_BUS_LOCK(bus);

	if (is_on) {
		udev->flags.remote_wakeup = 1;
	} else {
		udev->flags.remote_wakeup = 0;
	}

	USB_BUS_UNLOCK(bus);

#if USB_HAVE_POWERD
	/* In case we are out of sync, update the power state. */
	usb_bus_power_update(udev->bus);
#endif
	return (0);			/* success */
}

/*------------------------------------------------------------------------*
 *	usb_handle_request
 *
 * Internal state sequence:
 *
 * USB_HR_NOT_COMPLETE -> USB_HR_COMPLETE_OK v USB_HR_COMPLETE_ERR
 *
 * Returns:
 * 0: Ready to start hardware
 * Else: Stall current transfer, if any
 *------------------------------------------------------------------------*/
static usb_error_t
usb_handle_request(struct usb_xfer *xfer)
{
	struct usb_device_request req;
	struct usb_device *udev;
	const void *src_zcopy;		/* zero-copy source pointer */
	const void *src_mcopy;		/* non zero-copy source pointer */
	uint16_t off;			/* data offset */
	uint16_t rem;			/* data remainder */
	uint16_t max_len;		/* max fragment length */
	uint16_t wValue;
	uint8_t state;
	uint8_t is_complete = 1;
	usb_error_t err;
	union {
		uWord	wStatus;
		uint8_t	buf[2];
	}     temp;

	/*
	 * Filter the USB transfer state into
	 * something which we understand:
	 */

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
		state = USB_HR_NOT_COMPLETE;

		if (!xfer->flags_int.control_act) {
			/* nothing to do */
			goto tr_stalled;
		}
		break;
	case USB_ST_TRANSFERRED:
		if (!xfer->flags_int.control_act) {
			state = USB_HR_COMPLETE_OK;
		} else {
			state = USB_HR_NOT_COMPLETE;
		}
		break;
	default:
		state = USB_HR_COMPLETE_ERR;
		break;
	}

	/* reset frame stuff */

	usbd_xfer_set_frame_len(xfer, 0, 0);

	usbd_xfer_set_frame_offset(xfer, 0, 0);
	usbd_xfer_set_frame_offset(xfer, sizeof(req), 1);

	/* get the current request, if any */

	usbd_copy_out(xfer->frbuffers, 0, &req, sizeof(req));

	if (xfer->flags_int.control_rem == 0xFFFF) {
		/* first time - not initialised */
		rem = UGETW(req.wLength);
		off = 0;
	} else {
		/* not first time - initialised */
		rem = xfer->flags_int.control_rem;
		off = UGETW(req.wLength) - rem;
	}

	/* set some defaults */

	max_len = 0;
	src_zcopy = NULL;
	src_mcopy = NULL;
	udev = xfer->xroot->udev;

	/* get some request fields decoded */

	wValue = UGETW(req.wValue);

	DPRINTF("req 0x%02x 0x%02x 0x%04x 0x%04x "
	    "off=0x%x rem=0x%x, state=%d\n", req.bmRequestType,
	    req.bRequest, wValue, UGETW(req.wIndex), off, rem, state);

	/* demultiplex the control request */

	switch (req.bmRequestType) {
	case UT_READ_DEVICE:
		if (state != USB_HR_NOT_COMPLETE) {
			break;
		}
		switch (req.bRequest) {
		case UR_GET_DESCRIPTOR:
			goto tr_handle_get_descriptor;
		case UR_GET_CONFIG:
			goto tr_handle_get_config;
		case UR_GET_STATUS:
			goto tr_handle_get_status;
		default:
			goto tr_stalled;
		}
		break;

	case UT_WRITE_DEVICE:
		switch (req.bRequest) {
		case UR_SET_ADDRESS:
			goto tr_handle_set_address;
		case UR_SET_CONFIG:
			goto tr_handle_set_config;
		case UR_CLEAR_FEATURE:
			switch (wValue) {
			case UF_DEVICE_REMOTE_WAKEUP:
				goto tr_handle_clear_wakeup;
			default:
				goto tr_stalled;
			}
			break;
		case UR_SET_FEATURE:
			switch (wValue) {
			case UF_DEVICE_REMOTE_WAKEUP:
				goto tr_handle_set_wakeup;
			default:
				goto tr_stalled;
			}
			break;
		default:
			goto tr_stalled;
		}
		break;

	case UT_WRITE_ENDPOINT:
		switch (req.bRequest) {
		case UR_CLEAR_FEATURE:
			switch (wValue) {
			case UF_ENDPOINT_HALT:
				goto tr_handle_clear_halt;
			default:
				goto tr_stalled;
			}
			break;
		case UR_SET_FEATURE:
			switch (wValue) {
			case UF_ENDPOINT_HALT:
				goto tr_handle_set_halt;
			default:
				goto tr_stalled;
			}
			break;
		default:
			goto tr_stalled;
		}
		break;

	case UT_READ_ENDPOINT:
		switch (req.bRequest) {
		case UR_GET_STATUS:
			goto tr_handle_get_ep_status;
		default:
			goto tr_stalled;
		}
		break;
	default:
		/* we use "USB_ADD_BYTES" to de-const the src_zcopy */
		err = usb_handle_iface_request(xfer,
		    USB_ADD_BYTES(&src_zcopy, 0),
		    &max_len, req, off, state);
		if (err == 0) {
			is_complete = 0;
			goto tr_valid;
		} else if (err == USB_ERR_SHORT_XFER) {
			goto tr_valid;
		}
		/*
		 * Reset zero-copy pointer and max length
		 * variable in case they were unintentionally
		 * set:
		 */
		src_zcopy = NULL;
		max_len = 0;

		/*
		 * Check if we have a vendor specific
		 * descriptor:
		 */
		goto tr_handle_get_descriptor;
	}
	goto tr_valid;

tr_handle_get_descriptor:
	err = (usb_temp_get_desc_p) (udev, &req, &src_zcopy, &max_len);
	if (err)
		goto tr_stalled;
	if (src_zcopy == NULL)
		goto tr_stalled;
	goto tr_valid;

tr_handle_get_config:
	temp.buf[0] = udev->curr_config_no;
	src_mcopy = temp.buf;
	max_len = 1;
	goto tr_valid;

tr_handle_get_status:

	wValue = 0;

	USB_BUS_LOCK(udev->bus);
	if (udev->flags.remote_wakeup) {
		wValue |= UDS_REMOTE_WAKEUP;
	}
	if (udev->flags.self_powered) {
		wValue |= UDS_SELF_POWERED;
	}
	USB_BUS_UNLOCK(udev->bus);

	USETW(temp.wStatus, wValue);
	src_mcopy = temp.wStatus;
	max_len = sizeof(temp.wStatus);
	goto tr_valid;

tr_handle_set_address:
	if (state == USB_HR_NOT_COMPLETE) {
		if (wValue >= 0x80) {
			/* invalid value */
			goto tr_stalled;
		} else if (udev->curr_config_no != 0) {
			/* we are configured ! */
			goto tr_stalled;
		}
	} else if (state != USB_HR_NOT_COMPLETE) {
		udev->address = (wValue & 0x7F);
		goto tr_bad_context;
	}
	goto tr_valid;

tr_handle_set_config:
	if (state == USB_HR_NOT_COMPLETE) {
		if (usb_handle_set_config(xfer, req.wValue[0])) {
			goto tr_stalled;
		}
	}
	goto tr_valid;

tr_handle_clear_halt:
	if (state == USB_HR_NOT_COMPLETE) {
		if (usb_handle_set_stall(xfer, req.wIndex[0], 0)) {
			goto tr_stalled;
		}
	}
	goto tr_valid;

tr_handle_clear_wakeup:
	if (state == USB_HR_NOT_COMPLETE) {
		if (usb_handle_remote_wakeup(xfer, 0)) {
			goto tr_stalled;
		}
	}
	goto tr_valid;

tr_handle_set_halt:
	if (state == USB_HR_NOT_COMPLETE) {
		if (usb_handle_set_stall(xfer, req.wIndex[0], 1)) {
			goto tr_stalled;
		}
	}
	goto tr_valid;

tr_handle_set_wakeup:
	if (state == USB_HR_NOT_COMPLETE) {
		if (usb_handle_remote_wakeup(xfer, 1)) {
			goto tr_stalled;
		}
	}
	goto tr_valid;

tr_handle_get_ep_status:
	if (state == USB_HR_NOT_COMPLETE) {
		temp.wStatus[0] =
		    usb_handle_get_stall(udev, req.wIndex[0]);
		temp.wStatus[1] = 0;
		src_mcopy = temp.wStatus;
		max_len = sizeof(temp.wStatus);
	}
	goto tr_valid;

tr_valid:
	if (state != USB_HR_NOT_COMPLETE) {
		goto tr_stalled;
	}
	/* subtract offset from length */

	max_len -= off;

	/* Compute the real maximum data length */

	if (max_len > xfer->max_data_length) {
		max_len = usbd_xfer_max_len(xfer);
	}
	if (max_len > rem) {
		max_len = rem;
	}
	/*
	 * If the remainder is greater than the maximum data length,
	 * we need to truncate the value for the sake of the
	 * comparison below:
	 */
	if (rem > xfer->max_data_length) {
		rem = usbd_xfer_max_len(xfer);
	}
	if ((rem != max_len) && (is_complete != 0)) {
		/*
	         * If we don't transfer the data we can transfer, then
	         * the transfer is short !
	         */
		xfer->flags.force_short_xfer = 1;
		xfer->nframes = 2;
	} else {
		/*
		 * Default case
		 */
		xfer->flags.force_short_xfer = 0;
		xfer->nframes = max_len ? 2 : 1;
	}
	if (max_len > 0) {
		if (src_mcopy) {
			src_mcopy = USB_ADD_BYTES(src_mcopy, off);
			usbd_copy_in(xfer->frbuffers + 1, 0,
			    src_mcopy, max_len);
			usbd_xfer_set_frame_len(xfer, 1, max_len);
		} else {
			usbd_xfer_set_frame_data(xfer, 1,
			    USB_ADD_BYTES(src_zcopy, off), max_len);
		}
	} else {
		/* the end is reached, send status */
		xfer->flags.manual_status = 0;
		usbd_xfer_set_frame_len(xfer, 1, 0);
	}
	DPRINTF("success\n");
	return (0);			/* success */

tr_stalled:
	DPRINTF("%s\n", (state != USB_HR_NOT_COMPLETE) ?
	    "complete" : "stalled");
	return (USB_ERR_STALLED);

tr_bad_context:
	DPRINTF("bad context\n");
	return (USB_ERR_BAD_CONTEXT);
}
