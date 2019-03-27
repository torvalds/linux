/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 The NetBSD Foundation, Inc. All rights reserved.
 * Copyright (c) 1998 Lennart Augustsson. All rights reserved.
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
#include <dev/usb/usbhid.h>

#define	USB_DEBUG_VAR usb_debug

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_request.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_transfer.h>
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_device.h>
#include <dev/usb/usb_util.h>
#include <dev/usb/usb_dynamic.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#include <sys/ctype.h>
#endif			/* USB_GLOBAL_INCLUDE_FILE */

static int usb_no_cs_fail;

SYSCTL_INT(_hw_usb, OID_AUTO, no_cs_fail, CTLFLAG_RWTUN,
    &usb_no_cs_fail, 0, "USB clear stall failures are ignored, if set");

static int usb_full_ddesc;

SYSCTL_INT(_hw_usb, OID_AUTO, full_ddesc, CTLFLAG_RWTUN,
    &usb_full_ddesc, 0, "USB always read complete device descriptor, if set");

#ifdef USB_DEBUG
#ifdef USB_REQ_DEBUG
/* The following structures are used in connection to fault injection. */
struct usb_ctrl_debug {
	int bus_index;		/* target bus */
	int dev_index;		/* target address */
	int ds_fail;		/* fail data stage */
	int ss_fail;		/* fail status stage */
	int ds_delay;		/* data stage delay in ms */
	int ss_delay;		/* status stage delay in ms */
	int bmRequestType_value;
	int bRequest_value;
};

struct usb_ctrl_debug_bits {
	uint16_t ds_delay;
	uint16_t ss_delay;
	uint8_t ds_fail:1;
	uint8_t ss_fail:1;
	uint8_t enabled:1;
};

/* The default is to disable fault injection. */

static struct usb_ctrl_debug usb_ctrl_debug = {
	.bus_index = -1,
	.dev_index = -1,
	.bmRequestType_value = -1,
	.bRequest_value = -1,
};

SYSCTL_INT(_hw_usb, OID_AUTO, ctrl_bus_fail, CTLFLAG_RWTUN,
    &usb_ctrl_debug.bus_index, 0, "USB controller index to fail");
SYSCTL_INT(_hw_usb, OID_AUTO, ctrl_dev_fail, CTLFLAG_RWTUN,
    &usb_ctrl_debug.dev_index, 0, "USB device address to fail");
SYSCTL_INT(_hw_usb, OID_AUTO, ctrl_ds_fail, CTLFLAG_RWTUN,
    &usb_ctrl_debug.ds_fail, 0, "USB fail data stage");
SYSCTL_INT(_hw_usb, OID_AUTO, ctrl_ss_fail, CTLFLAG_RWTUN,
    &usb_ctrl_debug.ss_fail, 0, "USB fail status stage");
SYSCTL_INT(_hw_usb, OID_AUTO, ctrl_ds_delay, CTLFLAG_RWTUN,
    &usb_ctrl_debug.ds_delay, 0, "USB data stage delay in ms");
SYSCTL_INT(_hw_usb, OID_AUTO, ctrl_ss_delay, CTLFLAG_RWTUN,
    &usb_ctrl_debug.ss_delay, 0, "USB status stage delay in ms");
SYSCTL_INT(_hw_usb, OID_AUTO, ctrl_rt_fail, CTLFLAG_RWTUN,
    &usb_ctrl_debug.bmRequestType_value, 0, "USB bmRequestType to fail");
SYSCTL_INT(_hw_usb, OID_AUTO, ctrl_rv_fail, CTLFLAG_RWTUN,
    &usb_ctrl_debug.bRequest_value, 0, "USB bRequest to fail");

/*------------------------------------------------------------------------*
 *	usbd_get_debug_bits
 *
 * This function is only useful in USB host mode.
 *------------------------------------------------------------------------*/
static void
usbd_get_debug_bits(struct usb_device *udev, struct usb_device_request *req,
    struct usb_ctrl_debug_bits *dbg)
{
	int temp;

	memset(dbg, 0, sizeof(*dbg));

	/* Compute data stage delay */

	temp = usb_ctrl_debug.ds_delay;
	if (temp < 0)
		temp = 0;
	else if (temp > (16*1024))
		temp = (16*1024);

	dbg->ds_delay = temp;

	/* Compute status stage delay */

	temp = usb_ctrl_debug.ss_delay;
	if (temp < 0)
		temp = 0;
	else if (temp > (16*1024))
		temp = (16*1024);

	dbg->ss_delay = temp;

	/* Check if this control request should be failed */

	if (usbd_get_bus_index(udev) != usb_ctrl_debug.bus_index)
		return;

	if (usbd_get_device_index(udev) != usb_ctrl_debug.dev_index)
		return;

	temp = usb_ctrl_debug.bmRequestType_value;

	if ((temp != req->bmRequestType) && (temp >= 0) && (temp <= 255))
		return;

	temp = usb_ctrl_debug.bRequest_value;

	if ((temp != req->bRequest) && (temp >= 0) && (temp <= 255))
		return;

	temp = usb_ctrl_debug.ds_fail;
	if (temp)
		dbg->ds_fail = 1;

	temp = usb_ctrl_debug.ss_fail;
	if (temp)
		dbg->ss_fail = 1;

	dbg->enabled = 1;
}
#endif	/* USB_REQ_DEBUG */
#endif	/* USB_DEBUG */

/*------------------------------------------------------------------------*
 *	usbd_do_request_callback
 *
 * This function is the USB callback for generic USB Host control
 * transfers.
 *------------------------------------------------------------------------*/
void
usbd_do_request_callback(struct usb_xfer *xfer, usb_error_t error)
{
	;				/* workaround for a bug in "indent" */

	DPRINTF("st=%u\n", USB_GET_STATE(xfer));

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
		usbd_transfer_submit(xfer);
		break;
	default:
		cv_signal(&xfer->xroot->udev->ctrlreq_cv);
		break;
	}
}

/*------------------------------------------------------------------------*
 *	usb_do_clear_stall_callback
 *
 * This function is the USB callback for generic clear stall requests.
 *------------------------------------------------------------------------*/
void
usb_do_clear_stall_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct usb_device_request req;
	struct usb_device *udev;
	struct usb_endpoint *ep;
	struct usb_endpoint *ep_end;
	struct usb_endpoint *ep_first;
	usb_stream_t x;
	uint8_t to;

	udev = xfer->xroot->udev;

	USB_BUS_LOCK(udev->bus);

	/* round robin endpoint clear stall */

	ep = udev->ep_curr;
	ep_end = udev->endpoints + udev->endpoints_max;
	ep_first = udev->endpoints;
	to = udev->endpoints_max;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
tr_transferred:
		/* reset error counter */
		udev->clear_stall_errors = 0;

		if (ep == NULL)
			goto tr_setup;		/* device was unconfigured */
		if (ep->edesc &&
		    ep->is_stalled) {
			ep->toggle_next = 0;
			ep->is_stalled = 0;
			/* some hardware needs a callback to clear the data toggle */
			usbd_clear_stall_locked(udev, ep);
			for (x = 0; x != USB_MAX_EP_STREAMS; x++) {
				/* start the current or next transfer, if any */
				usb_command_wrapper(&ep->endpoint_q[x],
				    ep->endpoint_q[x].curr);
			}
		}
		ep++;

	case USB_ST_SETUP:
tr_setup:
		if (to == 0)
			break;			/* no endpoints - nothing to do */
		if ((ep < ep_first) || (ep >= ep_end))
			ep = ep_first;	/* endpoint wrapped around */
		if (ep->edesc &&
		    ep->is_stalled) {

			/* setup a clear-stall packet */

			req.bmRequestType = UT_WRITE_ENDPOINT;
			req.bRequest = UR_CLEAR_FEATURE;
			USETW(req.wValue, UF_ENDPOINT_HALT);
			req.wIndex[0] = ep->edesc->bEndpointAddress;
			req.wIndex[1] = 0;
			USETW(req.wLength, 0);

			/* copy in the transfer */

			usbd_copy_in(xfer->frbuffers, 0, &req, sizeof(req));

			/* set length */
			usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
			xfer->nframes = 1;
			USB_BUS_UNLOCK(udev->bus);

			usbd_transfer_submit(xfer);

			USB_BUS_LOCK(udev->bus);
			break;
		}
		ep++;
		to--;
		goto tr_setup;

	default:
		if (error == USB_ERR_CANCELLED)
			break;

		DPRINTF("Clear stall failed.\n");

		/*
		 * Some VMs like VirtualBox always return failure on
		 * clear-stall which we sometimes should just ignore.
		 */
		if (usb_no_cs_fail)
			goto tr_transferred;
		if (udev->clear_stall_errors == USB_CS_RESET_LIMIT)
			goto tr_setup;

		if (error == USB_ERR_TIMEOUT) {
			udev->clear_stall_errors = USB_CS_RESET_LIMIT;
			DPRINTF("Trying to re-enumerate.\n");
			usbd_start_re_enumerate(udev);
		} else {
			udev->clear_stall_errors++;
			if (udev->clear_stall_errors == USB_CS_RESET_LIMIT) {
				DPRINTF("Trying to re-enumerate.\n");
				usbd_start_re_enumerate(udev);
			}
		}
		goto tr_setup;
	}

	/* store current endpoint */
	udev->ep_curr = ep;
	USB_BUS_UNLOCK(udev->bus);
}

static usb_handle_req_t *
usbd_get_hr_func(struct usb_device *udev)
{
	/* figure out if there is a Handle Request function */
	if (udev->flags.usb_mode == USB_MODE_DEVICE)
		return (usb_temp_get_desc_p);
	else if (udev->parent_hub == NULL)
		return (udev->bus->methods->roothub_exec);
	else
		return (NULL);
}

/*------------------------------------------------------------------------*
 *	usbd_do_request_flags and usbd_do_request
 *
 * Description of arguments passed to these functions:
 *
 * "udev" - this is the "usb_device" structure pointer on which the
 * request should be performed. It is possible to call this function
 * in both Host Side mode and Device Side mode.
 *
 * "mtx" - if this argument is non-NULL the mutex pointed to by it
 * will get dropped and picked up during the execution of this
 * function, hence this function sometimes needs to sleep. If this
 * argument is NULL it has no effect.
 *
 * "req" - this argument must always be non-NULL and points to an
 * 8-byte structure holding the USB request to be done. The USB
 * request structure has a bit telling the direction of the USB
 * request, if it is a read or a write.
 *
 * "data" - if the "wLength" part of the structure pointed to by "req"
 * is non-zero this argument must point to a valid kernel buffer which
 * can hold at least "wLength" bytes. If "wLength" is zero "data" can
 * be NULL.
 *
 * "flags" - here is a list of valid flags:
 *
 *  o USB_SHORT_XFER_OK: allows the data transfer to be shorter than
 *  specified
 *
 *  o USB_DELAY_STATUS_STAGE: allows the status stage to be performed
 *  at a later point in time. This is tunable by the "hw.usb.ss_delay"
 *  sysctl. This flag is mostly useful for debugging.
 *
 *  o USB_USER_DATA_PTR: treat the "data" pointer like a userland
 *  pointer.
 *
 * "actlen" - if non-NULL the actual transfer length will be stored in
 * the 16-bit unsigned integer pointed to by "actlen". This
 * information is mostly useful when the "USB_SHORT_XFER_OK" flag is
 * used.
 *
 * "timeout" - gives the timeout for the control transfer in
 * milliseconds. A "timeout" value less than 50 milliseconds is
 * treated like a 50 millisecond timeout. A "timeout" value greater
 * than 30 seconds is treated like a 30 second timeout. This USB stack
 * does not allow control requests without a timeout.
 *
 * NOTE: This function is thread safe. All calls to "usbd_do_request_flags"
 * will be serialized by the use of the USB device enumeration lock.
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_do_request_flags(struct usb_device *udev, struct mtx *mtx,
    struct usb_device_request *req, void *data, uint16_t flags,
    uint16_t *actlen, usb_timeout_t timeout)
{
#ifdef USB_REQ_DEBUG
	struct usb_ctrl_debug_bits dbg;
#endif
	usb_handle_req_t *hr_func;
	struct usb_xfer *xfer;
	const void *desc;
	int err = 0;
	usb_ticks_t start_ticks;
	usb_ticks_t delta_ticks;
	usb_ticks_t max_ticks;
	uint16_t length;
	uint16_t temp;
	uint16_t acttemp;
	uint8_t do_unlock;

	if (timeout < 50) {
		/* timeout is too small */
		timeout = 50;
	}
	if (timeout > 30000) {
		/* timeout is too big */
		timeout = 30000;
	}
	length = UGETW(req->wLength);

	DPRINTFN(5, "udev=%p bmRequestType=0x%02x bRequest=0x%02x "
	    "wValue=0x%02x%02x wIndex=0x%02x%02x wLength=0x%02x%02x\n",
	    udev, req->bmRequestType, req->bRequest,
	    req->wValue[1], req->wValue[0],
	    req->wIndex[1], req->wIndex[0],
	    req->wLength[1], req->wLength[0]);

	/* Check if the device is still alive */
	if (udev->state < USB_STATE_POWERED) {
		DPRINTF("usb device has gone\n");
		return (USB_ERR_NOT_CONFIGURED);
	}

	/*
	 * Set "actlen" to a known value in case the caller does not
	 * check the return value:
	 */
	if (actlen)
		*actlen = 0;

#if (USB_HAVE_USER_IO == 0)
	if (flags & USB_USER_DATA_PTR)
		return (USB_ERR_INVAL);
#endif
	if ((mtx != NULL) && (mtx != &Giant)) {
		USB_MTX_UNLOCK(mtx);
		USB_MTX_ASSERT(mtx, MA_NOTOWNED);
	}

	/*
	 * Serialize access to this function:
	 */
	do_unlock = usbd_ctrl_lock(udev);

	hr_func = usbd_get_hr_func(udev);

	if (hr_func != NULL) {
		DPRINTF("Handle Request function is set\n");

		desc = NULL;
		temp = 0;

		if (!(req->bmRequestType & UT_READ)) {
			if (length != 0) {
				DPRINTFN(1, "The handle request function "
				    "does not support writing data!\n");
				err = USB_ERR_INVAL;
				goto done;
			}
		}

		/* The root HUB code needs the BUS lock locked */

		USB_BUS_LOCK(udev->bus);
		err = (hr_func) (udev, req, &desc, &temp);
		USB_BUS_UNLOCK(udev->bus);

		if (err)
			goto done;

		if (length > temp) {
			if (!(flags & USB_SHORT_XFER_OK)) {
				err = USB_ERR_SHORT_XFER;
				goto done;
			}
			length = temp;
		}
		if (actlen)
			*actlen = length;

		if (length > 0) {
#if USB_HAVE_USER_IO
			if (flags & USB_USER_DATA_PTR) {
				if (copyout(desc, data, length)) {
					err = USB_ERR_INVAL;
					goto done;
				}
			} else
#endif
				memcpy(data, desc, length);
		}
		goto done;		/* success */
	}

	/*
	 * Setup a new USB transfer or use the existing one, if any:
	 */
	usbd_ctrl_transfer_setup(udev);

	xfer = udev->ctrl_xfer[0];
	if (xfer == NULL) {
		/* most likely out of memory */
		err = USB_ERR_NOMEM;
		goto done;
	}

#ifdef USB_REQ_DEBUG
	/* Get debug bits */
	usbd_get_debug_bits(udev, req, &dbg);

	/* Check for fault injection */
	if (dbg.enabled)
		flags |= USB_DELAY_STATUS_STAGE;
#endif
	USB_XFER_LOCK(xfer);

	if (flags & USB_DELAY_STATUS_STAGE)
		xfer->flags.manual_status = 1;
	else
		xfer->flags.manual_status = 0;

	if (flags & USB_SHORT_XFER_OK)
		xfer->flags.short_xfer_ok = 1;
	else
		xfer->flags.short_xfer_ok = 0;

	xfer->timeout = timeout;

	start_ticks = ticks;

	max_ticks = USB_MS_TO_TICKS(timeout);

	usbd_copy_in(xfer->frbuffers, 0, req, sizeof(*req));

	usbd_xfer_set_frame_len(xfer, 0, sizeof(*req));

	while (1) {
		temp = length;
		if (temp > usbd_xfer_max_len(xfer)) {
			temp = usbd_xfer_max_len(xfer);
		}
#ifdef USB_REQ_DEBUG
		if (xfer->flags.manual_status) {
			if (usbd_xfer_frame_len(xfer, 0) != 0) {
				/* Execute data stage separately */
				temp = 0;
			} else if (temp > 0) {
				if (dbg.ds_fail) {
					err = USB_ERR_INVAL;
					break;
				}
				if (dbg.ds_delay > 0) {
					usb_pause_mtx(
					    xfer->xroot->xfer_mtx,
				            USB_MS_TO_TICKS(dbg.ds_delay));
					/* make sure we don't time out */
					start_ticks = ticks;
				}
			}
		}
#endif
		usbd_xfer_set_frame_len(xfer, 1, temp);

		if (temp > 0) {
			if (!(req->bmRequestType & UT_READ)) {
#if USB_HAVE_USER_IO
				if (flags & USB_USER_DATA_PTR) {
					USB_XFER_UNLOCK(xfer);
					err = usbd_copy_in_user(xfer->frbuffers + 1,
					    0, data, temp);
					USB_XFER_LOCK(xfer);
					if (err) {
						err = USB_ERR_INVAL;
						break;
					}
				} else
#endif
					usbd_copy_in(xfer->frbuffers + 1,
					    0, data, temp);
			}
			usbd_xfer_set_frames(xfer, 2);
		} else {
			if (usbd_xfer_frame_len(xfer, 0) == 0) {
				if (xfer->flags.manual_status) {
#ifdef USB_REQ_DEBUG
					if (dbg.ss_fail) {
						err = USB_ERR_INVAL;
						break;
					}
					if (dbg.ss_delay > 0) {
						usb_pause_mtx(
						    xfer->xroot->xfer_mtx,
						    USB_MS_TO_TICKS(dbg.ss_delay));
						/* make sure we don't time out */
						start_ticks = ticks;
					}
#endif
					xfer->flags.manual_status = 0;
				} else {
					break;
				}
			}
			usbd_xfer_set_frames(xfer, 1);
		}

		usbd_transfer_start(xfer);

		while (usbd_transfer_pending(xfer)) {
			cv_wait(&udev->ctrlreq_cv,
			    xfer->xroot->xfer_mtx);
		}

		err = xfer->error;

		if (err) {
			break;
		}

		/* get actual length of DATA stage */

		if (xfer->aframes < 2) {
			acttemp = 0;
		} else {
			acttemp = usbd_xfer_frame_len(xfer, 1);
		}

		/* check for short packet */

		if (temp > acttemp) {
			temp = acttemp;
			length = temp;
		}
		if (temp > 0) {
			if (req->bmRequestType & UT_READ) {
#if USB_HAVE_USER_IO
				if (flags & USB_USER_DATA_PTR) {
					USB_XFER_UNLOCK(xfer);
					err = usbd_copy_out_user(xfer->frbuffers + 1,
					    0, data, temp);
					USB_XFER_LOCK(xfer);
					if (err) {
						err = USB_ERR_INVAL;
						break;
					}
				} else
#endif
					usbd_copy_out(xfer->frbuffers + 1,
					    0, data, temp);
			}
		}
		/*
		 * Clear "frlengths[0]" so that we don't send the setup
		 * packet again:
		 */
		usbd_xfer_set_frame_len(xfer, 0, 0);

		/* update length and data pointer */
		length -= temp;
		data = USB_ADD_BYTES(data, temp);

		if (actlen) {
			(*actlen) += temp;
		}
		/* check for timeout */

		delta_ticks = ticks - start_ticks;
		if (delta_ticks > max_ticks) {
			if (!err) {
				err = USB_ERR_TIMEOUT;
			}
		}
		if (err) {
			break;
		}
	}

	if (err) {
		/*
		 * Make sure that the control endpoint is no longer
		 * blocked in case of a non-transfer related error:
		 */
		usbd_transfer_stop(xfer);
	}
	USB_XFER_UNLOCK(xfer);

done:
	if (do_unlock)
		usbd_ctrl_unlock(udev);

	if ((mtx != NULL) && (mtx != &Giant))
		USB_MTX_LOCK(mtx);

	switch (err) {
	case USB_ERR_NORMAL_COMPLETION:
	case USB_ERR_SHORT_XFER:
	case USB_ERR_STALLED:
	case USB_ERR_CANCELLED:
		break;
	default:
		DPRINTF("I/O error - waiting a bit for TT cleanup\n");
		usb_pause_mtx(mtx, hz / 16);
		break;
	}
	return ((usb_error_t)err);
}

/*------------------------------------------------------------------------*
 *	usbd_do_request_proc - factored out code
 *
 * This function is factored out code. It does basically the same like
 * usbd_do_request_flags, except it will check the status of the
 * passed process argument before doing the USB request. If the
 * process is draining the USB_ERR_IOERROR code will be returned. It
 * is assumed that the mutex associated with the process is locked
 * when calling this function.
 *------------------------------------------------------------------------*/
usb_error_t
usbd_do_request_proc(struct usb_device *udev, struct usb_process *pproc,
    struct usb_device_request *req, void *data, uint16_t flags,
    uint16_t *actlen, usb_timeout_t timeout)
{
	usb_error_t err;
	uint16_t len;

	/* get request data length */
	len = UGETW(req->wLength);

	/* check if the device is being detached */
	if (usb_proc_is_gone(pproc)) {
		err = USB_ERR_IOERROR;
		goto done;
	}

	/* forward the USB request */
	err = usbd_do_request_flags(udev, pproc->up_mtx,
	    req, data, flags, actlen, timeout);

done:
	/* on failure we zero the data */
	/* on short packet we zero the unused data */
	if ((len != 0) && (req->bmRequestType & UE_DIR_IN)) {
		if (err)
			memset(data, 0, len);
		else if (actlen && *actlen != len)
			memset(((uint8_t *)data) + *actlen, 0, len - *actlen);
	}
	return (err);
}

/*------------------------------------------------------------------------*
 *	usbd_req_reset_port
 *
 * This function will instruct a USB HUB to perform a reset sequence
 * on the specified port number.
 *
 * Returns:
 *    0: Success. The USB device should now be at address zero.
 * Else: Failure. No USB device is present and the USB port should be
 *       disabled.
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_reset_port(struct usb_device *udev, struct mtx *mtx, uint8_t port)
{
	struct usb_port_status ps;
	usb_error_t err;
	uint16_t n;
	uint16_t status;
	uint16_t change;

	DPRINTF("\n");

	/* clear any leftover port reset changes first */
	usbd_req_clear_port_feature(
	    udev, mtx, port, UHF_C_PORT_RESET);

	/* assert port reset on the given port */
	err = usbd_req_set_port_feature(
	    udev, mtx, port, UHF_PORT_RESET);

	/* check for errors */
	if (err)
		goto done;
	n = 0;
	while (1) {
		/* wait for the device to recover from reset */
		usb_pause_mtx(mtx, USB_MS_TO_TICKS(usb_port_reset_delay));
		n += usb_port_reset_delay;
		err = usbd_req_get_port_status(udev, mtx, &ps, port);
		if (err)
			goto done;

		status = UGETW(ps.wPortStatus);
		change = UGETW(ps.wPortChange);

		/* if the device disappeared, just give up */
		if (!(status & UPS_CURRENT_CONNECT_STATUS))
			goto done;

		/* check if reset is complete */
		if (change & UPS_C_PORT_RESET)
			break;

		/*
		 * Some Virtual Machines like VirtualBox 4.x fail to
		 * generate a port reset change event. Check if reset
		 * is no longer asserted.
		 */
		if (!(status & UPS_RESET))
			break;

		/* check for timeout */
		if (n > 1000) {
			n = 0;
			break;
		}
	}

	/* clear port reset first */
	err = usbd_req_clear_port_feature(
	    udev, mtx, port, UHF_C_PORT_RESET);
	if (err)
		goto done;

	/* check for timeout */
	if (n == 0) {
		err = USB_ERR_TIMEOUT;
		goto done;
	}
	/* wait for the device to recover from reset */
	usb_pause_mtx(mtx, USB_MS_TO_TICKS(usb_port_reset_recovery));

done:
	DPRINTFN(2, "port %d reset returning error=%s\n",
	    port, usbd_errstr(err));
	return (err);
}

/*------------------------------------------------------------------------*
 *	usbd_req_warm_reset_port
 *
 * This function will instruct an USB HUB to perform a warm reset
 * sequence on the specified port number. This kind of reset is not
 * mandatory for LOW-, FULL- and HIGH-speed USB HUBs and is targeted
 * for SUPER-speed USB HUBs.
 *
 * Returns:
 *    0: Success. The USB device should now be available again.
 * Else: Failure. No USB device is present and the USB port should be
 *       disabled.
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_warm_reset_port(struct usb_device *udev, struct mtx *mtx,
    uint8_t port)
{
	struct usb_port_status ps;
	usb_error_t err;
	uint16_t n;
	uint16_t status;
	uint16_t change;

	DPRINTF("\n");

	err = usbd_req_get_port_status(udev, mtx, &ps, port);
	if (err)
		goto done;

	status = UGETW(ps.wPortStatus);

	switch (UPS_PORT_LINK_STATE_GET(status)) {
	case UPS_PORT_LS_U3:
	case UPS_PORT_LS_COMP_MODE:
	case UPS_PORT_LS_LOOPBACK:
	case UPS_PORT_LS_SS_INA:
		break;
	default:
		DPRINTF("Wrong state for warm reset\n");
		return (0);
	}

	/* clear any leftover warm port reset changes first */
	usbd_req_clear_port_feature(udev, mtx,
	    port, UHF_C_BH_PORT_RESET);

	/* set warm port reset */
	err = usbd_req_set_port_feature(udev, mtx,
	    port, UHF_BH_PORT_RESET);
	if (err)
		goto done;

	n = 0;
	while (1) {
		/* wait for the device to recover from reset */
		usb_pause_mtx(mtx, USB_MS_TO_TICKS(usb_port_reset_delay));
		n += usb_port_reset_delay;
		err = usbd_req_get_port_status(udev, mtx, &ps, port);
		if (err)
			goto done;

		status = UGETW(ps.wPortStatus);
		change = UGETW(ps.wPortChange);

		/* if the device disappeared, just give up */
		if (!(status & UPS_CURRENT_CONNECT_STATUS))
			goto done;

		/* check if reset is complete */
		if (change & UPS_C_BH_PORT_RESET)
			break;

		/* check for timeout */
		if (n > 1000) {
			n = 0;
			break;
		}
	}

	/* clear port reset first */
	err = usbd_req_clear_port_feature(
	    udev, mtx, port, UHF_C_BH_PORT_RESET);
	if (err)
		goto done;

	/* check for timeout */
	if (n == 0) {
		err = USB_ERR_TIMEOUT;
		goto done;
	}
	/* wait for the device to recover from reset */
	usb_pause_mtx(mtx, USB_MS_TO_TICKS(usb_port_reset_recovery));

done:
	DPRINTFN(2, "port %d warm reset returning error=%s\n",
	    port, usbd_errstr(err));
	return (err);
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_desc
 *
 * This function can be used to retrieve USB descriptors. It contains
 * some additional logic like zeroing of missing descriptor bytes and
 * retrying an USB descriptor in case of failure. The "min_len"
 * argument specifies the minimum descriptor length. The "max_len"
 * argument specifies the maximum descriptor length. If the real
 * descriptor length is less than the minimum length the missing
 * byte(s) will be zeroed. The type field, the second byte of the USB
 * descriptor, will get forced to the correct type. If the "actlen"
 * pointer is non-NULL, the actual length of the transfer will get
 * stored in the 16-bit unsigned integer which it is pointing to. The
 * first byte of the descriptor will not get updated. If the "actlen"
 * pointer is NULL the first byte of the descriptor will get updated
 * to reflect the actual length instead. If "min_len" is not equal to
 * "max_len" then this function will try to retrive the beginning of
 * the descriptor and base the maximum length on the first byte of the
 * descriptor.
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_desc(struct usb_device *udev,
    struct mtx *mtx, uint16_t *actlen, void *desc,
    uint16_t min_len, uint16_t max_len,
    uint16_t id, uint8_t type, uint8_t index,
    uint8_t retries)
{
	struct usb_device_request req;
	uint8_t *buf = desc;
	usb_error_t err;

	DPRINTFN(4, "id=%d, type=%d, index=%d, max_len=%d\n",
	    id, type, index, max_len);

	req.bmRequestType = UT_READ_DEVICE;
	req.bRequest = UR_GET_DESCRIPTOR;
	USETW2(req.wValue, type, index);
	USETW(req.wIndex, id);

	while (1) {

		if ((min_len < 2) || (max_len < 2)) {
			err = USB_ERR_INVAL;
			goto done;
		}
		USETW(req.wLength, min_len);

		err = usbd_do_request_flags(udev, mtx, &req,
		    desc, 0, NULL, 500 /* ms */);

		if (err != 0 && err != USB_ERR_TIMEOUT &&
		    min_len != max_len) {
			/* clear descriptor data */
			memset(desc, 0, max_len);

			/* try to read full descriptor length */
			USETW(req.wLength, max_len);

			err = usbd_do_request_flags(udev, mtx, &req,
			    desc, USB_SHORT_XFER_OK, NULL, 500 /* ms */);

			if (err == 0) {
				/* verify length */
				if (buf[0] > max_len)
					buf[0] = max_len;
				else if (buf[0] < 2)
					err = USB_ERR_INVAL;

				min_len = buf[0];

				/* enforce descriptor type */
				buf[1] = type;
				goto done;
			}
		}

		if (err) {
			if (!retries) {
				goto done;
			}
			retries--;

			usb_pause_mtx(mtx, hz / 5);

			continue;
		}

		if (min_len == max_len) {

			/* enforce correct length */
			if ((buf[0] > min_len) && (actlen == NULL))
				buf[0] = min_len;

			/* enforce correct type */
			buf[1] = type;

			goto done;
		}
		/* range check */

		if (max_len > buf[0]) {
			max_len = buf[0];
		}
		/* zero minimum data */

		while (min_len > max_len) {
			min_len--;
			buf[min_len] = 0;
		}

		/* set new minimum length */

		min_len = max_len;
	}
done:
	if (actlen != NULL) {
		if (err)
			*actlen = 0;
		else
			*actlen = min_len;
	}
	return (err);
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_string_any
 *
 * This function will return the string given by "string_index"
 * using the first language ID. The maximum length "len" includes
 * the terminating zero. The "len" argument should be twice as
 * big pluss 2 bytes, compared with the actual maximum string length !
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_string_any(struct usb_device *udev, struct mtx *mtx, char *buf,
    uint16_t len, uint8_t string_index)
{
	char *s;
	uint8_t *temp;
	uint16_t i;
	uint16_t n;
	uint16_t c;
	uint8_t swap;
	usb_error_t err;

	if (len == 0) {
		/* should not happen */
		return (USB_ERR_NORMAL_COMPLETION);
	}
	if (string_index == 0) {
		/* this is the language table */
		buf[0] = 0;
		return (USB_ERR_INVAL);
	}
	if (udev->flags.no_strings) {
		buf[0] = 0;
		return (USB_ERR_STALLED);
	}
	err = usbd_req_get_string_desc
	    (udev, mtx, buf, len, udev->langid, string_index);
	if (err) {
		buf[0] = 0;
		return (err);
	}
	temp = (uint8_t *)buf;

	if (temp[0] < 2) {
		/* string length is too short */
		buf[0] = 0;
		return (USB_ERR_INVAL);
	}
	/* reserve one byte for terminating zero */
	len--;

	/* find maximum length */
	s = buf;
	n = (temp[0] / 2) - 1;
	if (n > len) {
		n = len;
	}
	/* skip descriptor header */
	temp += 2;

	/* reset swap state */
	swap = 3;

	/* convert and filter */
	for (i = 0; (i != n); i++) {
		c = UGETW(temp + (2 * i));

		/* convert from Unicode, handle buggy strings */
		if (((c & 0xff00) == 0) && (swap & 1)) {
			/* Little Endian, default */
			*s = c;
			swap = 1;
		} else if (((c & 0x00ff) == 0) && (swap & 2)) {
			/* Big Endian */
			*s = c >> 8;
			swap = 2;
		} else {
			/* silently skip bad character */
			continue;
		}

		/*
		 * Filter by default - We only allow alphanumerical
		 * and a few more to avoid any problems with scripts
		 * and daemons.
		 */
		if (isalpha(*s) ||
		    isdigit(*s) ||
		    *s == '-' ||
		    *s == '+' ||
		    *s == ' ' ||
		    *s == '.' ||
		    *s == ',' ||
		    *s == ':' ||
		    *s == '/' ||
		    *s == '(' ||
		    *s == ')') {
			/* allowed */
			s++;
		}
		/* silently skip bad character */
	}
	*s = 0;				/* zero terminate resulting string */
	return (USB_ERR_NORMAL_COMPLETION);
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_string_desc
 *
 * If you don't know the language ID, consider using
 * "usbd_req_get_string_any()".
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_string_desc(struct usb_device *udev, struct mtx *mtx, void *sdesc,
    uint16_t max_len, uint16_t lang_id,
    uint8_t string_index)
{
	return (usbd_req_get_desc(udev, mtx, NULL, sdesc, 2, max_len, lang_id,
	    UDESC_STRING, string_index, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_config_desc_ptr
 *
 * This function is used in device side mode to retrieve the pointer
 * to the generated config descriptor. This saves allocating space for
 * an additional config descriptor when setting the configuration.
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_descriptor_ptr(struct usb_device *udev,
    struct usb_config_descriptor **ppcd, uint16_t wValue)
{
	struct usb_device_request req;
	usb_handle_req_t *hr_func;
	const void *ptr;
	uint16_t len;
	usb_error_t err;

	req.bmRequestType = UT_READ_DEVICE;
	req.bRequest = UR_GET_DESCRIPTOR;
	USETW(req.wValue, wValue);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	ptr = NULL;
	len = 0;

	hr_func = usbd_get_hr_func(udev);

	if (hr_func == NULL)
		err = USB_ERR_INVAL;
	else {
		USB_BUS_LOCK(udev->bus);
		err = (hr_func) (udev, &req, &ptr, &len);
		USB_BUS_UNLOCK(udev->bus);
	}

	if (err)
		ptr = NULL;
	else if (ptr == NULL)
		err = USB_ERR_INVAL;

	*ppcd = __DECONST(struct usb_config_descriptor *, ptr);

	return (err);
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_config_desc
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_config_desc(struct usb_device *udev, struct mtx *mtx,
    struct usb_config_descriptor *d, uint8_t conf_index)
{
	usb_error_t err;

	DPRINTFN(4, "confidx=%d\n", conf_index);

	err = usbd_req_get_desc(udev, mtx, NULL, d, sizeof(*d),
	    sizeof(*d), 0, UDESC_CONFIG, conf_index, 0);
	if (err) {
		goto done;
	}
	/* Extra sanity checking */
	if (UGETW(d->wTotalLength) < (uint16_t)sizeof(*d)) {
		err = USB_ERR_INVAL;
	}
done:
	return (err);
}

/*------------------------------------------------------------------------*
 *	usbd_alloc_config_desc
 *
 * This function is used to allocate a zeroed configuration
 * descriptor.
 *
 * Returns:
 * NULL: Failure
 * Else: Success
 *------------------------------------------------------------------------*/
void *
usbd_alloc_config_desc(struct usb_device *udev, uint32_t size)
{
	if (size > USB_CONFIG_MAX) {
		DPRINTF("Configuration descriptor too big\n");
		return (NULL);
	}
#if (USB_HAVE_FIXED_CONFIG == 0)
	return (malloc(size, M_USBDEV, M_ZERO | M_WAITOK));
#else
	memset(udev->config_data, 0, sizeof(udev->config_data));
	return (udev->config_data);
#endif
}

/*------------------------------------------------------------------------*
 *	usbd_alloc_config_desc
 *
 * This function is used to free a configuration descriptor.
 *------------------------------------------------------------------------*/
void
usbd_free_config_desc(struct usb_device *udev, void *ptr)
{
#if (USB_HAVE_FIXED_CONFIG == 0)
	free(ptr, M_USBDEV);
#endif
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_config_desc_full
 *
 * This function gets the complete USB configuration descriptor and
 * ensures that "wTotalLength" is correct. The returned configuration
 * descriptor is freed by calling "usbd_free_config_desc()".
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_config_desc_full(struct usb_device *udev, struct mtx *mtx,
    struct usb_config_descriptor **ppcd, uint8_t index)
{
	struct usb_config_descriptor cd;
	struct usb_config_descriptor *cdesc;
	uint32_t len;
	usb_error_t err;

	DPRINTFN(4, "index=%d\n", index);

	*ppcd = NULL;

	err = usbd_req_get_config_desc(udev, mtx, &cd, index);
	if (err)
		return (err);

	/* get full descriptor */
	len = UGETW(cd.wTotalLength);
	if (len < (uint32_t)sizeof(*cdesc)) {
		/* corrupt descriptor */
		return (USB_ERR_INVAL);
	} else if (len > USB_CONFIG_MAX) {
		DPRINTF("Configuration descriptor was truncated\n");
		len = USB_CONFIG_MAX;
	}
	cdesc = usbd_alloc_config_desc(udev, len);
	if (cdesc == NULL)
		return (USB_ERR_NOMEM);
	err = usbd_req_get_desc(udev, mtx, NULL, cdesc, len, len, 0,
	    UDESC_CONFIG, index, 3);
	if (err) {
		usbd_free_config_desc(udev, cdesc);
		return (err);
	}
	/* make sure that the device is not fooling us: */
	USETW(cdesc->wTotalLength, len);

	*ppcd = cdesc;

	return (0);			/* success */
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_device_desc
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_device_desc(struct usb_device *udev, struct mtx *mtx,
    struct usb_device_descriptor *d)
{
	DPRINTFN(4, "\n");
	return (usbd_req_get_desc(udev, mtx, NULL, d, sizeof(*d),
	    sizeof(*d), 0, UDESC_DEVICE, 0, 3));
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_alt_interface_no
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_alt_interface_no(struct usb_device *udev, struct mtx *mtx,
    uint8_t *alt_iface_no, uint8_t iface_index)
{
	struct usb_interface *iface = usbd_get_iface(udev, iface_index);
	struct usb_device_request req;

	if ((iface == NULL) || (iface->idesc == NULL))
		return (USB_ERR_INVAL);

	req.bmRequestType = UT_READ_INTERFACE;
	req.bRequest = UR_GET_INTERFACE;
	USETW(req.wValue, 0);
	req.wIndex[0] = iface->idesc->bInterfaceNumber;
	req.wIndex[1] = 0;
	USETW(req.wLength, 1);
	return (usbd_do_request(udev, mtx, &req, alt_iface_no));
}

/*------------------------------------------------------------------------*
 *	usbd_req_set_alt_interface_no
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_alt_interface_no(struct usb_device *udev, struct mtx *mtx,
    uint8_t iface_index, uint8_t alt_no)
{
	struct usb_interface *iface = usbd_get_iface(udev, iface_index);
	struct usb_device_request req;

	if ((iface == NULL) || (iface->idesc == NULL))
		return (USB_ERR_INVAL);

	req.bmRequestType = UT_WRITE_INTERFACE;
	req.bRequest = UR_SET_INTERFACE;
	req.wValue[0] = alt_no;
	req.wValue[1] = 0;
	req.wIndex[0] = iface->idesc->bInterfaceNumber;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_device_status
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_device_status(struct usb_device *udev, struct mtx *mtx,
    struct usb_status *st)
{
	struct usb_device_request req;

	req.bmRequestType = UT_READ_DEVICE;
	req.bRequest = UR_GET_STATUS;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, sizeof(*st));
	return (usbd_do_request(udev, mtx, &req, st));
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_hub_descriptor
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_hub_descriptor(struct usb_device *udev, struct mtx *mtx,
    struct usb_hub_descriptor *hd, uint8_t nports)
{
	struct usb_device_request req;
	uint16_t len = (nports + 7 + (8 * 8)) / 8;

	req.bmRequestType = UT_READ_CLASS_DEVICE;
	req.bRequest = UR_GET_DESCRIPTOR;
	USETW2(req.wValue, UDESC_HUB, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);
	return (usbd_do_request(udev, mtx, &req, hd));
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_ss_hub_descriptor
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_ss_hub_descriptor(struct usb_device *udev, struct mtx *mtx,
    struct usb_hub_ss_descriptor *hd, uint8_t nports)
{
	struct usb_device_request req;
	uint16_t len = sizeof(*hd) - 32 + 1 + ((nports + 7) / 8);

	req.bmRequestType = UT_READ_CLASS_DEVICE;
	req.bRequest = UR_GET_DESCRIPTOR;
	USETW2(req.wValue, UDESC_SS_HUB, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);
	return (usbd_do_request(udev, mtx, &req, hd));
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_hub_status
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_hub_status(struct usb_device *udev, struct mtx *mtx,
    struct usb_hub_status *st)
{
	struct usb_device_request req;

	req.bmRequestType = UT_READ_CLASS_DEVICE;
	req.bRequest = UR_GET_STATUS;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, sizeof(struct usb_hub_status));
	return (usbd_do_request(udev, mtx, &req, st));
}

/*------------------------------------------------------------------------*
 *	usbd_req_set_address
 *
 * This function is used to set the address for an USB device. After
 * port reset the USB device will respond at address zero.
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_address(struct usb_device *udev, struct mtx *mtx, uint16_t addr)
{
	struct usb_device_request req;
	usb_error_t err;

	DPRINTFN(6, "setting device address=%d\n", addr);

	req.bmRequestType = UT_WRITE_DEVICE;
	req.bRequest = UR_SET_ADDRESS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);

	err = USB_ERR_INVAL;

	/* check if USB controller handles set address */
	if (udev->bus->methods->set_address != NULL)
		err = (udev->bus->methods->set_address) (udev, mtx, addr);

	if (err != USB_ERR_INVAL)
		goto done;

	/* Setting the address should not take more than 1 second ! */
	err = usbd_do_request_flags(udev, mtx, &req, NULL,
	    USB_DELAY_STATUS_STAGE, NULL, 1000);

done:
	/* allow device time to set new address */
	usb_pause_mtx(mtx,
	    USB_MS_TO_TICKS(usb_set_address_settle));

	return (err);
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_port_status
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_port_status(struct usb_device *udev, struct mtx *mtx,
    struct usb_port_status *ps, uint8_t port)
{
	struct usb_device_request req;

	req.bmRequestType = UT_READ_CLASS_OTHER;
	req.bRequest = UR_GET_STATUS;
	USETW(req.wValue, 0);
	req.wIndex[0] = port;
	req.wIndex[1] = 0;
	USETW(req.wLength, sizeof(*ps));

	return (usbd_do_request_flags(udev, mtx, &req, ps, 0, NULL, 1000));
}

/*------------------------------------------------------------------------*
 *	usbd_req_clear_hub_feature
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_clear_hub_feature(struct usb_device *udev, struct mtx *mtx,
    uint16_t sel)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_CLASS_DEVICE;
	req.bRequest = UR_CLEAR_FEATURE;
	USETW(req.wValue, sel);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_set_hub_feature
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_hub_feature(struct usb_device *udev, struct mtx *mtx,
    uint16_t sel)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_CLASS_DEVICE;
	req.bRequest = UR_SET_FEATURE;
	USETW(req.wValue, sel);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_set_hub_u1_timeout
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_hub_u1_timeout(struct usb_device *udev, struct mtx *mtx,
    uint8_t port, uint8_t timeout)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_CLASS_OTHER;
	req.bRequest = UR_SET_FEATURE;
	USETW(req.wValue, UHF_PORT_U1_TIMEOUT);
	req.wIndex[0] = port;
	req.wIndex[1] = timeout;
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_set_hub_u2_timeout
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_hub_u2_timeout(struct usb_device *udev, struct mtx *mtx,
    uint8_t port, uint8_t timeout)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_CLASS_OTHER;
	req.bRequest = UR_SET_FEATURE;
	USETW(req.wValue, UHF_PORT_U2_TIMEOUT);
	req.wIndex[0] = port;
	req.wIndex[1] = timeout;
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_set_hub_depth
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_hub_depth(struct usb_device *udev, struct mtx *mtx,
    uint16_t depth)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_CLASS_DEVICE;
	req.bRequest = UR_SET_HUB_DEPTH;
	USETW(req.wValue, depth);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_clear_port_feature
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_clear_port_feature(struct usb_device *udev, struct mtx *mtx,
    uint8_t port, uint16_t sel)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_CLASS_OTHER;
	req.bRequest = UR_CLEAR_FEATURE;
	USETW(req.wValue, sel);
	req.wIndex[0] = port;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_set_port_feature
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_port_feature(struct usb_device *udev, struct mtx *mtx,
    uint8_t port, uint16_t sel)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_CLASS_OTHER;
	req.bRequest = UR_SET_FEATURE;
	USETW(req.wValue, sel);
	req.wIndex[0] = port;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_set_protocol
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_protocol(struct usb_device *udev, struct mtx *mtx,
    uint8_t iface_index, uint16_t report)
{
	struct usb_interface *iface = usbd_get_iface(udev, iface_index);
	struct usb_device_request req;

	if ((iface == NULL) || (iface->idesc == NULL)) {
		return (USB_ERR_INVAL);
	}
	DPRINTFN(5, "iface=%p, report=%d, endpt=%d\n",
	    iface, report, iface->idesc->bInterfaceNumber);

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UR_SET_PROTOCOL;
	USETW(req.wValue, report);
	req.wIndex[0] = iface->idesc->bInterfaceNumber;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_set_report
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_report(struct usb_device *udev, struct mtx *mtx, void *data, uint16_t len,
    uint8_t iface_index, uint8_t type, uint8_t id)
{
	struct usb_interface *iface = usbd_get_iface(udev, iface_index);
	struct usb_device_request req;

	if ((iface == NULL) || (iface->idesc == NULL)) {
		return (USB_ERR_INVAL);
	}
	DPRINTFN(5, "len=%d\n", len);

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UR_SET_REPORT;
	USETW2(req.wValue, type, id);
	req.wIndex[0] = iface->idesc->bInterfaceNumber;
	req.wIndex[1] = 0;
	USETW(req.wLength, len);
	return (usbd_do_request(udev, mtx, &req, data));
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_report
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_report(struct usb_device *udev, struct mtx *mtx, void *data,
    uint16_t len, uint8_t iface_index, uint8_t type, uint8_t id)
{
	struct usb_interface *iface = usbd_get_iface(udev, iface_index);
	struct usb_device_request req;

	if ((iface == NULL) || (iface->idesc == NULL)) {
		return (USB_ERR_INVAL);
	}
	DPRINTFN(5, "len=%d\n", len);

	req.bmRequestType = UT_READ_CLASS_INTERFACE;
	req.bRequest = UR_GET_REPORT;
	USETW2(req.wValue, type, id);
	req.wIndex[0] = iface->idesc->bInterfaceNumber;
	req.wIndex[1] = 0;
	USETW(req.wLength, len);
	return (usbd_do_request(udev, mtx, &req, data));
}

/*------------------------------------------------------------------------*
 *	usbd_req_set_idle
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_idle(struct usb_device *udev, struct mtx *mtx,
    uint8_t iface_index, uint8_t duration, uint8_t id)
{
	struct usb_interface *iface = usbd_get_iface(udev, iface_index);
	struct usb_device_request req;

	if ((iface == NULL) || (iface->idesc == NULL)) {
		return (USB_ERR_INVAL);
	}
	DPRINTFN(5, "%d %d\n", duration, id);

	req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
	req.bRequest = UR_SET_IDLE;
	USETW2(req.wValue, duration, id);
	req.wIndex[0] = iface->idesc->bInterfaceNumber;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_report_descriptor
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_report_descriptor(struct usb_device *udev, struct mtx *mtx,
    void *d, uint16_t size, uint8_t iface_index)
{
	struct usb_interface *iface = usbd_get_iface(udev, iface_index);
	struct usb_device_request req;

	if ((iface == NULL) || (iface->idesc == NULL)) {
		return (USB_ERR_INVAL);
	}
	req.bmRequestType = UT_READ_INTERFACE;
	req.bRequest = UR_GET_DESCRIPTOR;
	USETW2(req.wValue, UDESC_REPORT, 0);	/* report id should be 0 */
	req.wIndex[0] = iface->idesc->bInterfaceNumber;
	req.wIndex[1] = 0;
	USETW(req.wLength, size);
	return (usbd_do_request(udev, mtx, &req, d));
}

/*------------------------------------------------------------------------*
 *	usbd_req_set_config
 *
 * This function is used to select the current configuration number in
 * both USB device side mode and USB host side mode. When setting the
 * configuration the function of the interfaces can change.
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_config(struct usb_device *udev, struct mtx *mtx, uint8_t conf)
{
	struct usb_device_request req;

	DPRINTF("setting config %d\n", conf);

	/* do "set configuration" request */

	req.bmRequestType = UT_WRITE_DEVICE;
	req.bRequest = UR_SET_CONFIG;
	req.wValue[0] = conf;
	req.wValue[1] = 0;
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_get_config
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_get_config(struct usb_device *udev, struct mtx *mtx, uint8_t *pconf)
{
	struct usb_device_request req;

	req.bmRequestType = UT_READ_DEVICE;
	req.bRequest = UR_GET_CONFIG;
	USETW(req.wValue, 0);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 1);
	return (usbd_do_request(udev, mtx, &req, pconf));
}

/*------------------------------------------------------------------------*
 *	usbd_setup_device_desc
 *------------------------------------------------------------------------*/
usb_error_t
usbd_setup_device_desc(struct usb_device *udev, struct mtx *mtx)
{
	usb_error_t err;

	/*
	 * Get the first 8 bytes of the device descriptor !
	 *
	 * NOTE: "usbd_do_request()" will check the device descriptor
	 * next time we do a request to see if the maximum packet size
	 * changed! The 8 first bytes of the device descriptor
	 * contains the maximum packet size to use on control endpoint
	 * 0. If this value is different from "USB_MAX_IPACKET" a new
	 * USB control request will be setup!
	 */
	switch (udev->speed) {
	case USB_SPEED_FULL:
		if (usb_full_ddesc != 0) {
			/* get full device descriptor */
			err = usbd_req_get_device_desc(udev, mtx, &udev->ddesc);
			if (err == 0)
				break;
		}

		/* get partial device descriptor, some devices crash on this */
		err = usbd_req_get_desc(udev, mtx, NULL, &udev->ddesc,
		    USB_MAX_IPACKET, USB_MAX_IPACKET, 0, UDESC_DEVICE, 0, 0);
		if (err != 0)
			break;

		/* get the full device descriptor */
		err = usbd_req_get_device_desc(udev, mtx, &udev->ddesc);
		break;

	default:
		DPRINTF("Minimum bMaxPacketSize is large enough "
		    "to hold the complete device descriptor or "
		    "only one bMaxPacketSize choice\n");

		/* get the full device descriptor */
		err = usbd_req_get_device_desc(udev, mtx, &udev->ddesc);

		/* try one more time, if error */
		if (err != 0)
			err = usbd_req_get_device_desc(udev, mtx, &udev->ddesc);
		break;
	}

	if (err != 0) {
		DPRINTFN(0, "getting device descriptor "
		    "at addr %d failed, %s\n", udev->address,
		    usbd_errstr(err));
		return (err);
	}

	DPRINTF("adding unit addr=%d, rev=%02x, class=%d, "
	    "subclass=%d, protocol=%d, maxpacket=%d, len=%d, speed=%d\n",
	    udev->address, UGETW(udev->ddesc.bcdUSB),
	    udev->ddesc.bDeviceClass,
	    udev->ddesc.bDeviceSubClass,
	    udev->ddesc.bDeviceProtocol,
	    udev->ddesc.bMaxPacketSize,
	    udev->ddesc.bLength,
	    udev->speed);

	return (err);
}

/*------------------------------------------------------------------------*
 *	usbd_req_re_enumerate
 *
 * NOTE: After this function returns the hardware is in the
 * unconfigured state! The application is responsible for setting a
 * new configuration.
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_re_enumerate(struct usb_device *udev, struct mtx *mtx)
{
	struct usb_device *parent_hub;
	usb_error_t err;
	uint8_t old_addr;
	uint8_t do_retry = 1;

	if (udev->flags.usb_mode != USB_MODE_HOST) {
		return (USB_ERR_INVAL);
	}
	old_addr = udev->address;
	parent_hub = udev->parent_hub;
	if (parent_hub == NULL) {
		return (USB_ERR_INVAL);
	}
retry:
#if USB_HAVE_TT_SUPPORT
	/*
	 * Try to reset the High Speed parent HUB of a LOW- or FULL-
	 * speed device, if any.
	 */
	if (udev->parent_hs_hub != NULL &&
	    udev->speed != USB_SPEED_HIGH) {
		DPRINTF("Trying to reset parent High Speed TT.\n");
		if (udev->parent_hs_hub == parent_hub &&
		    (uhub_count_active_host_ports(parent_hub, USB_SPEED_LOW) +
		     uhub_count_active_host_ports(parent_hub, USB_SPEED_FULL)) == 1) {
			/* we can reset the whole TT */
			err = usbd_req_reset_tt(parent_hub, NULL,
			    udev->hs_port_no);
		} else {
			/* only reset a particular device and endpoint */
			err = usbd_req_clear_tt_buffer(udev->parent_hs_hub, NULL,
			    udev->hs_port_no, old_addr, UE_CONTROL, 0);
		}
		if (err) {
			DPRINTF("Resetting parent High "
			    "Speed TT failed (%s).\n",
			    usbd_errstr(err));
		}
	}
#endif
	/* Try to warm reset first */
	if (parent_hub->speed == USB_SPEED_SUPER)
		usbd_req_warm_reset_port(parent_hub, mtx, udev->port_no);

	/* Try to reset the parent HUB port. */
	err = usbd_req_reset_port(parent_hub, mtx, udev->port_no);
	if (err) {
		DPRINTFN(0, "addr=%d, port reset failed, %s\n", 
		    old_addr, usbd_errstr(err));
		goto done;
	}

	/*
	 * After that the port has been reset our device should be at
	 * address zero:
	 */
	udev->address = USB_START_ADDR;

	/* reset "bMaxPacketSize" */
	udev->ddesc.bMaxPacketSize = USB_MAX_IPACKET;

	/* reset USB state */
	usb_set_device_state(udev, USB_STATE_POWERED);

	/*
	 * Restore device address:
	 */
	err = usbd_req_set_address(udev, mtx, old_addr);
	if (err) {
		/* XXX ignore any errors! */
		DPRINTFN(0, "addr=%d, set address failed! (%s, ignored)\n",
		    old_addr, usbd_errstr(err));
	}
	/*
	 * Restore device address, if the controller driver did not
	 * set a new one:
	 */
	if (udev->address == USB_START_ADDR)
		udev->address = old_addr;

	/* setup the device descriptor and the initial "wMaxPacketSize" */
	err = usbd_setup_device_desc(udev, mtx);

done:
	if (err && do_retry) {
		/* give the USB firmware some time to load */
		usb_pause_mtx(mtx, hz / 2);
		/* no more retries after this retry */
		do_retry = 0;
		/* try again */
		goto retry;
	}
	/* restore address */
	if (udev->address == USB_START_ADDR)
		udev->address = old_addr;
	/* update state, if successful */
	if (err == 0)
		usb_set_device_state(udev, USB_STATE_ADDRESSED);
	return (err);
}

/*------------------------------------------------------------------------*
 *	usbd_req_clear_device_feature
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_clear_device_feature(struct usb_device *udev, struct mtx *mtx,
    uint16_t sel)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_DEVICE;
	req.bRequest = UR_CLEAR_FEATURE;
	USETW(req.wValue, sel);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_set_device_feature
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_device_feature(struct usb_device *udev, struct mtx *mtx,
    uint16_t sel)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_DEVICE;
	req.bRequest = UR_SET_FEATURE;
	USETW(req.wValue, sel);
	USETW(req.wIndex, 0);
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_reset_tt
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_reset_tt(struct usb_device *udev, struct mtx *mtx,
    uint8_t port)
{
	struct usb_device_request req;

	/* For single TT HUBs the port should be 1 */

	if (udev->ddesc.bDeviceClass == UDCLASS_HUB &&
	    udev->ddesc.bDeviceProtocol == UDPROTO_HSHUBSTT)
		port = 1;

	req.bmRequestType = UT_WRITE_CLASS_OTHER;
	req.bRequest = UR_RESET_TT;
	USETW(req.wValue, 0);
	req.wIndex[0] = port;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_clear_tt_buffer
 *
 * For single TT HUBs the port should be 1.
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_clear_tt_buffer(struct usb_device *udev, struct mtx *mtx,
    uint8_t port, uint8_t addr, uint8_t type, uint8_t endpoint)
{
	struct usb_device_request req;
	uint16_t wValue;

	/* For single TT HUBs the port should be 1 */

	if (udev->ddesc.bDeviceClass == UDCLASS_HUB &&
	    udev->ddesc.bDeviceProtocol == UDPROTO_HSHUBSTT)
		port = 1;

	wValue = (endpoint & 0xF) | ((addr & 0x7F) << 4) |
	    ((endpoint & 0x80) << 8) | ((type & 3) << 12);

	req.bmRequestType = UT_WRITE_CLASS_OTHER;
	req.bRequest = UR_CLEAR_TT_BUFFER;
	USETW(req.wValue, wValue);
	req.wIndex[0] = port;
	req.wIndex[1] = 0;
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *	usbd_req_set_port_link_state
 *
 * USB 3.0 specific request
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_port_link_state(struct usb_device *udev, struct mtx *mtx,
    uint8_t port, uint8_t link_state)
{
	struct usb_device_request req;

	req.bmRequestType = UT_WRITE_CLASS_OTHER;
	req.bRequest = UR_SET_FEATURE;
	USETW(req.wValue, UHF_PORT_LINK_STATE);
	req.wIndex[0] = port;
	req.wIndex[1] = link_state;
	USETW(req.wLength, 0);
	return (usbd_do_request(udev, mtx, &req, 0));
}

/*------------------------------------------------------------------------*
 *		usbd_req_set_lpm_info
 *
 * USB 2.0 specific request for Link Power Management.
 *
 * Returns:
 * 0:				Success
 * USB_ERR_PENDING_REQUESTS:	NYET
 * USB_ERR_TIMEOUT:		TIMEOUT
 * USB_ERR_STALL:		STALL
 * Else:			Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_req_set_lpm_info(struct usb_device *udev, struct mtx *mtx,
    uint8_t port, uint8_t besl, uint8_t addr, uint8_t rwe)
{
	struct usb_device_request req;
	usb_error_t err;
	uint8_t buf[1];

	req.bmRequestType = UT_WRITE_CLASS_OTHER;
	req.bRequest = UR_SET_AND_TEST;
	USETW(req.wValue, UHF_PORT_L1);
	req.wIndex[0] = (port & 0xF) | ((besl & 0xF) << 4);
	req.wIndex[1] = (addr & 0x7F) | (rwe ? 0x80 : 0x00);
	USETW(req.wLength, sizeof(buf));

	/* set default value in case of short transfer */
	buf[0] = 0x00;

	err = usbd_do_request(udev, mtx, &req, buf);
	if (err)
		return (err);

	switch (buf[0]) {
	case 0x00:	/* SUCCESS */
		break;
	case 0x10:	/* NYET */
		err = USB_ERR_PENDING_REQUESTS;
		break;
	case 0x11:	/* TIMEOUT */
		err = USB_ERR_TIMEOUT;
		break;
	case 0x30:	/* STALL */
		err = USB_ERR_STALLED;
		break;
	default:	/* reserved */
		err = USB_ERR_IOERROR;
		break;
	}
	return (err);
}

