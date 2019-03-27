/* $FreeBSD$ */
/*-
 * Copyright (c) 2007 Luigi Rizzo - Universita` di Pisa. All rights reserved.
 * Copyright (c) 2007 Hans Petter Selasky. All rights reserved.
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

#define	USB_DEBUG_VAR usb_debug

#include <dev/usb/usb_core.h>
#include <linux/usb.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_device.h>
#include <dev/usb/usb_util.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_transfer.h>
#include <dev/usb/usb_hub.h>
#include <dev/usb/usb_request.h>
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_dynamic.h>
#endif			/* USB_GLOBAL_INCLUDE_FILE */

struct usb_linux_softc {
	LIST_ENTRY(usb_linux_softc) sc_attached_list;

	device_t sc_fbsd_dev;
	struct usb_device *sc_fbsd_udev;
	struct usb_interface *sc_ui;
	struct usb_driver *sc_udrv;
};

/* prototypes */
static device_probe_t usb_linux_probe;
static device_attach_t usb_linux_attach;
static device_detach_t usb_linux_detach;
static device_suspend_t usb_linux_suspend;
static device_resume_t usb_linux_resume;

static usb_callback_t usb_linux_isoc_callback;
static usb_callback_t usb_linux_non_isoc_callback;

static usb_complete_t usb_linux_wait_complete;

static uint16_t	usb_max_isoc_frames(struct usb_device *);
static int	usb_start_wait_urb(struct urb *, usb_timeout_t, uint16_t *);
static const struct usb_device_id *usb_linux_lookup_id(
		    const struct usb_device_id *, struct usb_attach_arg *);
static struct	usb_driver *usb_linux_get_usb_driver(struct usb_linux_softc *);
static int	usb_linux_create_usb_device(struct usb_device *, device_t);
static void	usb_linux_cleanup_interface(struct usb_device *,
		    struct usb_interface *);
static void	usb_linux_complete(struct usb_xfer *);
static int	usb_unlink_urb_sub(struct urb *, uint8_t);

/*------------------------------------------------------------------------*
 * FreeBSD USB interface
 *------------------------------------------------------------------------*/

static LIST_HEAD(, usb_linux_softc) usb_linux_attached_list;
static LIST_HEAD(, usb_driver) usb_linux_driver_list;

static device_method_t usb_linux_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, usb_linux_probe),
	DEVMETHOD(device_attach, usb_linux_attach),
	DEVMETHOD(device_detach, usb_linux_detach),
	DEVMETHOD(device_suspend, usb_linux_suspend),
	DEVMETHOD(device_resume, usb_linux_resume),

	DEVMETHOD_END
};

static driver_t usb_linux_driver = {
	.name = "usb_linux",
	.methods = usb_linux_methods,
	.size = sizeof(struct usb_linux_softc),
};

static devclass_t usb_linux_devclass;

DRIVER_MODULE(usb_linux, uhub, usb_linux_driver, usb_linux_devclass, NULL, 0);
MODULE_VERSION(usb_linux, 1);

/*------------------------------------------------------------------------*
 *	usb_linux_lookup_id
 *
 * This functions takes an array of "struct usb_device_id" and tries
 * to match the entries with the information in "struct usb_attach_arg".
 * If it finds a match the matching entry will be returned.
 * Else "NULL" will be returned.
 *------------------------------------------------------------------------*/
static const struct usb_device_id *
usb_linux_lookup_id(const struct usb_device_id *id, struct usb_attach_arg *uaa)
{
	if (id == NULL) {
		goto done;
	}
	/*
	 * Keep on matching array entries until we find one with
	 * "match_flags" equal to zero, which indicates the end of the
	 * array:
	 */
	for (; id->match_flags; id++) {

		if ((id->match_flags & USB_DEVICE_ID_MATCH_VENDOR) &&
		    (id->idVendor != uaa->info.idVendor)) {
			continue;
		}
		if ((id->match_flags & USB_DEVICE_ID_MATCH_PRODUCT) &&
		    (id->idProduct != uaa->info.idProduct)) {
			continue;
		}
		if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_LO) &&
		    (id->bcdDevice_lo > uaa->info.bcdDevice)) {
			continue;
		}
		if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_HI) &&
		    (id->bcdDevice_hi < uaa->info.bcdDevice)) {
			continue;
		}
		if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_CLASS) &&
		    (id->bDeviceClass != uaa->info.bDeviceClass)) {
			continue;
		}
		if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_SUBCLASS) &&
		    (id->bDeviceSubClass != uaa->info.bDeviceSubClass)) {
			continue;
		}
		if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_PROTOCOL) &&
		    (id->bDeviceProtocol != uaa->info.bDeviceProtocol)) {
			continue;
		}
		if ((uaa->info.bDeviceClass == 0xFF) &&
		    !(id->match_flags & USB_DEVICE_ID_MATCH_VENDOR) &&
		    (id->match_flags & (USB_DEVICE_ID_MATCH_INT_CLASS |
		    USB_DEVICE_ID_MATCH_INT_SUBCLASS |
		    USB_DEVICE_ID_MATCH_INT_PROTOCOL))) {
			continue;
		}
		if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_CLASS) &&
		    (id->bInterfaceClass != uaa->info.bInterfaceClass)) {
			continue;
		}
		if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_SUBCLASS) &&
		    (id->bInterfaceSubClass != uaa->info.bInterfaceSubClass)) {
			continue;
		}
		if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_PROTOCOL) &&
		    (id->bInterfaceProtocol != uaa->info.bInterfaceProtocol)) {
			continue;
		}
		/* we found a match! */
		return (id);
	}

done:
	return (NULL);
}

/*------------------------------------------------------------------------*
 *	usb_linux_probe
 *
 * This function is the FreeBSD probe callback. It is called from the
 * FreeBSD USB stack through the "device_probe_and_attach()" function.
 *------------------------------------------------------------------------*/
static int
usb_linux_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct usb_driver *udrv;
	int err = ENXIO;

	if (uaa->usb_mode != USB_MODE_HOST) {
		return (ENXIO);
	}
	mtx_lock(&Giant);
	LIST_FOREACH(udrv, &usb_linux_driver_list, linux_driver_list) {
		if (usb_linux_lookup_id(udrv->id_table, uaa)) {
			err = 0;
			break;
		}
	}
	mtx_unlock(&Giant);

	return (err);
}

/*------------------------------------------------------------------------*
 *	usb_linux_get_usb_driver
 *
 * This function returns the pointer to the "struct usb_driver" where
 * the Linux USB device driver "struct usb_device_id" match was found.
 * We apply a lock before reading out the pointer to avoid races.
 *------------------------------------------------------------------------*/
static struct usb_driver *
usb_linux_get_usb_driver(struct usb_linux_softc *sc)
{
	struct usb_driver *udrv;

	mtx_lock(&Giant);
	udrv = sc->sc_udrv;
	mtx_unlock(&Giant);
	return (udrv);
}

/*------------------------------------------------------------------------*
 *	usb_linux_attach
 *
 * This function is the FreeBSD attach callback. It is called from the
 * FreeBSD USB stack through the "device_probe_and_attach()" function.
 * This function is called when "usb_linux_probe()" returns zero.
 *------------------------------------------------------------------------*/
static int
usb_linux_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct usb_linux_softc *sc = device_get_softc(dev);
	struct usb_driver *udrv;
	const struct usb_device_id *id = NULL;

	mtx_lock(&Giant);
	LIST_FOREACH(udrv, &usb_linux_driver_list, linux_driver_list) {
		id = usb_linux_lookup_id(udrv->id_table, uaa);
		if (id)
			break;
	}
	mtx_unlock(&Giant);

	if (id == NULL) {
		return (ENXIO);
	}
	if (usb_linux_create_usb_device(uaa->device, dev) != 0)
		return (ENOMEM);
	device_set_usb_desc(dev);

	sc->sc_fbsd_udev = uaa->device;
	sc->sc_fbsd_dev = dev;
	sc->sc_udrv = udrv;
	sc->sc_ui = usb_ifnum_to_if(uaa->device, uaa->info.bIfaceNum);
	if (sc->sc_ui == NULL) {
		return (EINVAL);
	}
	if (udrv->probe) {
		if ((udrv->probe) (sc->sc_ui, id)) {
			return (ENXIO);
		}
	}
	mtx_lock(&Giant);
	LIST_INSERT_HEAD(&usb_linux_attached_list, sc, sc_attached_list);
	mtx_unlock(&Giant);

	/* success */
	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_linux_detach
 *
 * This function is the FreeBSD detach callback. It is called from the
 * FreeBSD USB stack through the "device_detach()" function.
 *------------------------------------------------------------------------*/
static int
usb_linux_detach(device_t dev)
{
	struct usb_linux_softc *sc = device_get_softc(dev);
	struct usb_driver *udrv = NULL;

	mtx_lock(&Giant);
	if (sc->sc_attached_list.le_prev) {
		LIST_REMOVE(sc, sc_attached_list);
		sc->sc_attached_list.le_prev = NULL;
		udrv = sc->sc_udrv;
		sc->sc_udrv = NULL;
	}
	mtx_unlock(&Giant);

	if (udrv && udrv->disconnect) {
		(udrv->disconnect) (sc->sc_ui);
	}
	/*
	 * Make sure that we free all FreeBSD USB transfers belonging to
	 * this Linux "usb_interface", hence they will most likely not be
	 * needed any more.
	 */
	usb_linux_cleanup_interface(sc->sc_fbsd_udev, sc->sc_ui);
	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_linux_suspend
 *
 * This function is the FreeBSD suspend callback. Usually it does nothing.
 *------------------------------------------------------------------------*/
static int
usb_linux_suspend(device_t dev)
{
	struct usb_linux_softc *sc = device_get_softc(dev);
	struct usb_driver *udrv = usb_linux_get_usb_driver(sc);
	int err;

	if (udrv && udrv->suspend) {
		err = (udrv->suspend) (sc->sc_ui, 0);
	}
	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_linux_resume
 *
 * This function is the FreeBSD resume callback. Usually it does nothing.
 *------------------------------------------------------------------------*/
static int
usb_linux_resume(device_t dev)
{
	struct usb_linux_softc *sc = device_get_softc(dev);
	struct usb_driver *udrv = usb_linux_get_usb_driver(sc);
	int err;

	if (udrv && udrv->resume) {
		err = (udrv->resume) (sc->sc_ui);
	}
	return (0);
}

/*------------------------------------------------------------------------*
 * Linux emulation layer
 *------------------------------------------------------------------------*/

/*------------------------------------------------------------------------*
 *	usb_max_isoc_frames
 *
 * The following function returns the maximum number of isochronous
 * frames that we support per URB. It is not part of the Linux USB API.
 *------------------------------------------------------------------------*/
static uint16_t
usb_max_isoc_frames(struct usb_device *dev)
{
	;				/* indent fix */
	switch (usbd_get_speed(dev)) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
		return (USB_MAX_FULL_SPEED_ISOC_FRAMES);
	default:
		return (USB_MAX_HIGH_SPEED_ISOC_FRAMES);
	}
}

/*------------------------------------------------------------------------*
 *	usb_submit_urb
 *
 * This function is used to queue an URB after that it has been
 * initialized. If it returns non-zero, it means that the URB was not
 * queued.
 *------------------------------------------------------------------------*/
int
usb_submit_urb(struct urb *urb, uint16_t mem_flags)
{
	struct usb_host_endpoint *uhe;
	uint8_t do_unlock;
	int err;

	if (urb == NULL)
		return (-EINVAL);

	do_unlock = mtx_owned(&Giant) ? 0 : 1;
	if (do_unlock)
		mtx_lock(&Giant);

	if (urb->endpoint == NULL) {
		err = -EINVAL;
		goto done;
	}

	/*
	 * Check to see if the urb is in the process of being killed
	 * and stop a urb that is in the process of being killed from
	 * being re-submitted (e.g. from its completion callback
	 * function).
	 */
	if (urb->kill_count != 0) {
		err = -EPERM;
		goto done;
	}

	uhe = urb->endpoint;

	/*
	 * Check that we have got a FreeBSD USB transfer that will dequeue
	 * the URB structure and do the real transfer. If there are no USB
	 * transfers, then we return an error.
	 */
	if (uhe->bsd_xfer[0] ||
	    uhe->bsd_xfer[1]) {
		/* we are ready! */

		TAILQ_INSERT_TAIL(&uhe->bsd_urb_list, urb, bsd_urb_list);

		urb->status = -EINPROGRESS;

		usbd_transfer_start(uhe->bsd_xfer[0]);
		usbd_transfer_start(uhe->bsd_xfer[1]);
		err = 0;
	} else {
		/* no pipes have been setup yet! */
		urb->status = -EINVAL;
		err = -EINVAL;
	}
done:
	if (do_unlock)
		mtx_unlock(&Giant);
	return (err);
}

/*------------------------------------------------------------------------*
 *	usb_unlink_urb
 *
 * This function is used to stop an URB after that it is been
 * submitted, but before the "complete" callback has been called. On
 *------------------------------------------------------------------------*/
int
usb_unlink_urb(struct urb *urb)
{
	return (usb_unlink_urb_sub(urb, 0));
}

static void
usb_unlink_bsd(struct usb_xfer *xfer,
    struct urb *urb, uint8_t drain)
{
	if (xfer == NULL)
		return;
	if (!usbd_transfer_pending(xfer))
		return;
	if (xfer->priv_fifo == (void *)urb) {
		if (drain) {
			mtx_unlock(&Giant);
			usbd_transfer_drain(xfer);
			mtx_lock(&Giant);
		} else {
			usbd_transfer_stop(xfer);
		}
		usbd_transfer_start(xfer);
	}
}

static int
usb_unlink_urb_sub(struct urb *urb, uint8_t drain)
{
	struct usb_host_endpoint *uhe;
	uint16_t x;
	uint8_t do_unlock;
	int err;

	if (urb == NULL)
		return (-EINVAL);

	do_unlock = mtx_owned(&Giant) ? 0 : 1;
	if (do_unlock)
		mtx_lock(&Giant);
	if (drain)
		urb->kill_count++;

	if (urb->endpoint == NULL) {
		err = -EINVAL;
		goto done;
	}
	uhe = urb->endpoint;

	if (urb->bsd_urb_list.tqe_prev) {

		/* not started yet, just remove it from the queue */
		TAILQ_REMOVE(&uhe->bsd_urb_list, urb, bsd_urb_list);
		urb->bsd_urb_list.tqe_prev = NULL;
		urb->status = -ECONNRESET;
		urb->actual_length = 0;

		for (x = 0; x < urb->number_of_packets; x++) {
			urb->iso_frame_desc[x].actual_length = 0;
		}

		if (urb->complete) {
			(urb->complete) (urb);
		}
	} else {

		/*
		 * If the URB is not on the URB list, then check if one of
		 * the FreeBSD USB transfer are processing the current URB.
		 * If so, re-start that transfer, which will lead to the
		 * termination of that URB:
		 */
		usb_unlink_bsd(uhe->bsd_xfer[0], urb, drain);
		usb_unlink_bsd(uhe->bsd_xfer[1], urb, drain);
	}
	err = 0;
done:
	if (drain)
		urb->kill_count--;
	if (do_unlock)
		mtx_unlock(&Giant);
	return (err);
}

/*------------------------------------------------------------------------*
 *	usb_clear_halt
 *
 * This function must always be used to clear the stall. Stall is when
 * an USB endpoint returns a stall message to the USB host controller.
 * Until the stall is cleared, no data can be transferred.
 *------------------------------------------------------------------------*/
int
usb_clear_halt(struct usb_device *dev, struct usb_host_endpoint *uhe)
{
	struct usb_config cfg[1];
	struct usb_endpoint *ep;
	uint8_t type;
	uint8_t addr;

	if (uhe == NULL)
		return (-EINVAL);

	type = uhe->desc.bmAttributes & UE_XFERTYPE;
	addr = uhe->desc.bEndpointAddress;

	memset(cfg, 0, sizeof(cfg));

	cfg[0].type = type;
	cfg[0].endpoint = addr & UE_ADDR;
	cfg[0].direction = addr & (UE_DIR_OUT | UE_DIR_IN);

	ep = usbd_get_endpoint(dev, uhe->bsd_iface_index, cfg);
	if (ep == NULL)
		return (-EINVAL);

	usbd_clear_data_toggle(dev, ep);

	return (usb_control_msg(dev, &dev->ep0,
	    UR_CLEAR_FEATURE, UT_WRITE_ENDPOINT,
	    UF_ENDPOINT_HALT, addr, NULL, 0, 1000));
}

/*------------------------------------------------------------------------*
 *	usb_start_wait_urb
 *
 * This is an internal function that is used to perform synchronous
 * Linux USB transfers.
 *------------------------------------------------------------------------*/
static int
usb_start_wait_urb(struct urb *urb, usb_timeout_t timeout, uint16_t *p_actlen)
{
	int err;
	uint8_t do_unlock;

	/* you must have a timeout! */
	if (timeout == 0) {
		timeout = 1;
	}
	urb->complete = &usb_linux_wait_complete;
	urb->timeout = timeout;
	urb->transfer_flags |= URB_WAIT_WAKEUP;
	urb->transfer_flags &= ~URB_IS_SLEEPING;

	do_unlock = mtx_owned(&Giant) ? 0 : 1;
	if (do_unlock)
		mtx_lock(&Giant);
	err = usb_submit_urb(urb, 0);
	if (err)
		goto done;

	/*
	 * the URB might have completed before we get here, so check that by
	 * using some flags!
	 */
	while (urb->transfer_flags & URB_WAIT_WAKEUP) {
		urb->transfer_flags |= URB_IS_SLEEPING;
		cv_wait(&urb->cv_wait, &Giant);
		urb->transfer_flags &= ~URB_IS_SLEEPING;
	}

	err = urb->status;

done:
	if (do_unlock)
		mtx_unlock(&Giant);
	if (p_actlen != NULL) {
		if (err)
			*p_actlen = 0;
		else
			*p_actlen = urb->actual_length;
	}
	return (err);
}

/*------------------------------------------------------------------------*
 *	usb_control_msg
 *
 * The following function performs a control transfer sequence one any
 * control, bulk or interrupt endpoint, specified by "uhe". A control
 * transfer means that you transfer an 8-byte header first followed by
 * a data-phase as indicated by the 8-byte header. The "timeout" is
 * given in milliseconds.
 *
 * Return values:
 *   0: Success
 * < 0: Failure
 * > 0: Actual length
 *------------------------------------------------------------------------*/
int
usb_control_msg(struct usb_device *dev, struct usb_host_endpoint *uhe,
    uint8_t request, uint8_t requesttype,
    uint16_t value, uint16_t index, void *data,
    uint16_t size, usb_timeout_t timeout)
{
	struct usb_device_request req;
	struct urb *urb;
	int err;
	uint16_t actlen;
	uint8_t type;
	uint8_t addr;

	req.bmRequestType = requesttype;
	req.bRequest = request;
	USETW(req.wValue, value);
	USETW(req.wIndex, index);
	USETW(req.wLength, size);

	if (uhe == NULL) {
		return (-EINVAL);
	}
	type = (uhe->desc.bmAttributes & UE_XFERTYPE);
	addr = (uhe->desc.bEndpointAddress & UE_ADDR);

	if (type != UE_CONTROL) {
		return (-EINVAL);
	}
	if (addr == 0) {
		/*
		 * The FreeBSD USB stack supports standard control
		 * transfers on control endpoint zero:
		 */
		err = usbd_do_request_flags(dev,
		    NULL, &req, data, USB_SHORT_XFER_OK,
		    &actlen, timeout);
		if (err) {
			err = -EPIPE;
		} else {
			err = actlen;
		}
		return (err);
	}
	if (dev->flags.usb_mode != USB_MODE_HOST) {
		/* not supported */
		return (-EINVAL);
	}
	err = usb_setup_endpoint(dev, uhe, 1 /* dummy */ );

	/*
	 * NOTE: we need to allocate real memory here so that we don't
	 * transfer data to/from the stack!
	 *
	 * 0xFFFF is a FreeBSD specific magic value.
	 */
	urb = usb_alloc_urb(0xFFFF, size);
	if (urb == NULL)
		return (-ENOMEM);

	urb->dev = dev;
	urb->endpoint = uhe;

	memcpy(urb->setup_packet, &req, sizeof(req));

	if (size && (!(req.bmRequestType & UT_READ))) {
		/* move the data to a real buffer */
		memcpy(USB_ADD_BYTES(urb->setup_packet, sizeof(req)),
		    data, size);
	}
	err = usb_start_wait_urb(urb, timeout, &actlen);

	if (req.bmRequestType & UT_READ) {
		if (actlen) {
			bcopy(USB_ADD_BYTES(urb->setup_packet,
			    sizeof(req)), data, actlen);
		}
	}
	usb_free_urb(urb);

	if (err == 0) {
		err = actlen;
	}
	return (err);
}

/*------------------------------------------------------------------------*
 *	usb_set_interface
 *
 * The following function will select which alternate setting of an
 * USB interface you plan to use. By default alternate setting with
 * index zero is selected. Note that "iface_no" is not the interface
 * index, but rather the value of "bInterfaceNumber".
 *------------------------------------------------------------------------*/
int
usb_set_interface(struct usb_device *dev, uint8_t iface_no, uint8_t alt_index)
{
	struct usb_interface *p_ui = usb_ifnum_to_if(dev, iface_no);
	int err;

	if (p_ui == NULL)
		return (-EINVAL);
	if (alt_index >= p_ui->num_altsetting)
		return (-EINVAL);
	usb_linux_cleanup_interface(dev, p_ui);
	err = -usbd_set_alt_interface_index(dev,
	    p_ui->bsd_iface_index, alt_index);
	if (err == 0) {
		p_ui->cur_altsetting = p_ui->altsetting + alt_index;
	}
	return (err);
}

/*------------------------------------------------------------------------*
 *	usb_setup_endpoint
 *
 * The following function is an extension to the Linux USB API that
 * allows you to set a maximum buffer size for a given USB endpoint.
 * The maximum buffer size is per URB. If you don't call this function
 * to set a maximum buffer size, the endpoint will not be functional.
 * Note that for isochronous endpoints the maximum buffer size must be
 * a non-zero dummy, hence this function will base the maximum buffer
 * size on "wMaxPacketSize".
 *------------------------------------------------------------------------*/
int
usb_setup_endpoint(struct usb_device *dev,
    struct usb_host_endpoint *uhe, usb_size_t bufsize)
{
	struct usb_config cfg[2];
	uint8_t type = uhe->desc.bmAttributes & UE_XFERTYPE;
	uint8_t addr = uhe->desc.bEndpointAddress;

	if (uhe->fbsd_buf_size == bufsize) {
		/* optimize */
		return (0);
	}
	usbd_transfer_unsetup(uhe->bsd_xfer, 2);

	uhe->fbsd_buf_size = bufsize;

	if (bufsize == 0) {
		return (0);
	}
	memset(cfg, 0, sizeof(cfg));

	if (type == UE_ISOCHRONOUS) {

		/*
		 * Isochronous transfers are special in that they don't fit
		 * into the BULK/INTR/CONTROL transfer model.
		 */

		cfg[0].type = type;
		cfg[0].endpoint = addr & UE_ADDR;
		cfg[0].direction = addr & (UE_DIR_OUT | UE_DIR_IN);
		cfg[0].callback = &usb_linux_isoc_callback;
		cfg[0].bufsize = 0;	/* use wMaxPacketSize */
		cfg[0].frames = usb_max_isoc_frames(dev);
		cfg[0].flags.proxy_buffer = 1;
#if 0
		/*
		 * The Linux USB API allows non back-to-back
		 * isochronous frames which we do not support. If the
		 * isochronous frames are not back-to-back we need to
		 * do a copy, and then we need a buffer for
		 * that. Enable this at your own risk.
		 */
		cfg[0].flags.ext_buffer = 1;
#endif
		cfg[0].flags.short_xfer_ok = 1;

		bcopy(cfg, cfg + 1, sizeof(*cfg));

		/* Allocate and setup two generic FreeBSD USB transfers */

		if (usbd_transfer_setup(dev, &uhe->bsd_iface_index,
		    uhe->bsd_xfer, cfg, 2, uhe, &Giant)) {
			return (-EINVAL);
		}
	} else {
		if (bufsize > (1 << 22)) {
			/* limit buffer size */
			bufsize = (1 << 22);
		}
		/* Allocate and setup one generic FreeBSD USB transfer */

		cfg[0].type = type;
		cfg[0].endpoint = addr & UE_ADDR;
		cfg[0].direction = addr & (UE_DIR_OUT | UE_DIR_IN);
		cfg[0].callback = &usb_linux_non_isoc_callback;
		cfg[0].bufsize = bufsize;
		cfg[0].flags.ext_buffer = 1;	/* enable zero-copy */
		cfg[0].flags.proxy_buffer = 1;
		cfg[0].flags.short_xfer_ok = 1;

		if (usbd_transfer_setup(dev, &uhe->bsd_iface_index,
		    uhe->bsd_xfer, cfg, 1, uhe, &Giant)) {
			return (-EINVAL);
		}
	}
	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_linux_create_usb_device
 *
 * The following function is used to build up a per USB device
 * structure tree, that mimics the Linux one. The root structure
 * is returned by this function.
 *------------------------------------------------------------------------*/
static int
usb_linux_create_usb_device(struct usb_device *udev, device_t dev)
{
	struct usb_config_descriptor *cd = usbd_get_config_descriptor(udev);
	struct usb_descriptor *desc;
	struct usb_interface_descriptor *id;
	struct usb_endpoint_descriptor *ed;
	struct usb_interface *p_ui = NULL;
	struct usb_host_interface *p_uhi = NULL;
	struct usb_host_endpoint *p_uhe = NULL;
	usb_size_t size;
	uint16_t niface_total;
	uint16_t nedesc;
	uint16_t iface_no_curr;
	uint16_t iface_index;
	uint8_t pass;
	uint8_t iface_no;

	/*
	 * We do two passes. One pass for computing necessary memory size
	 * and one pass to initialize all the allocated memory structures.
	 */
	for (pass = 0; pass < 2; pass++) {

		iface_no_curr = 0xFFFF;
		niface_total = 0;
		iface_index = 0;
		nedesc = 0;
		desc = NULL;

		/*
		 * Iterate over all the USB descriptors. Use the USB config
		 * descriptor pointer provided by the FreeBSD USB stack.
		 */
		while ((desc = usb_desc_foreach(cd, desc))) {

			/*
			 * Build up a tree according to the descriptors we
			 * find:
			 */
			switch (desc->bDescriptorType) {
			case UDESC_DEVICE:
				break;

			case UDESC_ENDPOINT:
				ed = (void *)desc;
				if ((ed->bLength < sizeof(*ed)) ||
				    (iface_index == 0))
					break;
				if (p_uhe) {
					bcopy(ed, &p_uhe->desc, sizeof(p_uhe->desc));
					p_uhe->bsd_iface_index = iface_index - 1;
					TAILQ_INIT(&p_uhe->bsd_urb_list);
					p_uhe++;
				}
				if (p_uhi) {
					(p_uhi - 1)->desc.bNumEndpoints++;
				}
				nedesc++;
				break;

			case UDESC_INTERFACE:
				id = (void *)desc;
				if (id->bLength < sizeof(*id))
					break;
				if (p_uhi) {
					bcopy(id, &p_uhi->desc, sizeof(p_uhi->desc));
					p_uhi->desc.bNumEndpoints = 0;
					p_uhi->endpoint = p_uhe;
					p_uhi->string = "";
					p_uhi->bsd_iface_index = iface_index;
					p_uhi++;
				}
				iface_no = id->bInterfaceNumber;
				niface_total++;
				if (iface_no_curr != iface_no) {
					if (p_ui) {
						p_ui->altsetting = p_uhi - 1;
						p_ui->cur_altsetting = p_uhi - 1;
						p_ui->num_altsetting = 1;
						p_ui->bsd_iface_index = iface_index;
						p_ui->linux_udev = udev;
						p_ui++;
					}
					iface_no_curr = iface_no;
					iface_index++;
				} else {
					if (p_ui) {
						(p_ui - 1)->num_altsetting++;
					}
				}
				break;

			default:
				break;
			}
		}

		if (pass == 0) {

			size = (sizeof(*p_uhe) * nedesc) +
			    (sizeof(*p_ui) * iface_index) +
			    (sizeof(*p_uhi) * niface_total);

			p_uhe = malloc(size, M_USBDEV, M_WAITOK | M_ZERO);
			p_ui = (void *)(p_uhe + nedesc);
			p_uhi = (void *)(p_ui + iface_index);

			udev->linux_iface_start = p_ui;
			udev->linux_iface_end = p_ui + iface_index;
			udev->linux_endpoint_start = p_uhe;
			udev->linux_endpoint_end = p_uhe + nedesc;
			udev->devnum = device_get_unit(dev);
			bcopy(&udev->ddesc, &udev->descriptor,
			    sizeof(udev->descriptor));
			bcopy(udev->ctrl_ep.edesc, &udev->ep0.desc,
			    sizeof(udev->ep0.desc));
		}
	}
	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_alloc_urb
 *
 * This function should always be used when you allocate an URB for
 * use with the USB Linux stack. In case of an isochronous transfer
 * you must specifiy the maximum number of "iso_packets" which you
 * plan to transfer per URB. This function is always blocking, and
 * "mem_flags" are not regarded like on Linux.
 *------------------------------------------------------------------------*/
struct urb *
usb_alloc_urb(uint16_t iso_packets, uint16_t mem_flags)
{
	struct urb *urb;
	usb_size_t size;

	if (iso_packets == 0xFFFF) {
		/*
		 * FreeBSD specific magic value to ask for control transfer
		 * memory allocation:
		 */
		size = sizeof(*urb) + sizeof(struct usb_device_request) + mem_flags;
	} else {
		size = sizeof(*urb) + (iso_packets * sizeof(urb->iso_frame_desc[0]));
	}

	urb = malloc(size, M_USBDEV, M_WAITOK | M_ZERO);
	if (urb) {

		cv_init(&urb->cv_wait, "URBWAIT");
		if (iso_packets == 0xFFFF) {
			urb->setup_packet = (void *)(urb + 1);
			urb->transfer_buffer = (void *)(urb->setup_packet +
			    sizeof(struct usb_device_request));
		} else {
			urb->number_of_packets = iso_packets;
		}
	}
	return (urb);
}

/*------------------------------------------------------------------------*
 *	usb_find_host_endpoint
 *
 * The following function will return the Linux USB host endpoint
 * structure that matches the given endpoint type and endpoint
 * value. If no match is found, NULL is returned. This function is not
 * part of the Linux USB API and is only used internally.
 *------------------------------------------------------------------------*/
struct usb_host_endpoint *
usb_find_host_endpoint(struct usb_device *dev, uint8_t type, uint8_t ep)
{
	struct usb_host_endpoint *uhe;
	struct usb_host_endpoint *uhe_end;
	struct usb_host_interface *uhi;
	struct usb_interface *ui;
	uint8_t ea;
	uint8_t at;
	uint8_t mask;

	if (dev == NULL) {
		return (NULL);
	}
	if (type == UE_CONTROL) {
		mask = UE_ADDR;
	} else {
		mask = (UE_DIR_IN | UE_DIR_OUT | UE_ADDR);
	}

	ep &= mask;

	/*
	 * Iterate over all the interfaces searching the selected alternate
	 * setting only, and all belonging endpoints.
	 */
	for (ui = dev->linux_iface_start;
	    ui != dev->linux_iface_end;
	    ui++) {
		uhi = ui->cur_altsetting;
		if (uhi) {
			uhe_end = uhi->endpoint + uhi->desc.bNumEndpoints;
			for (uhe = uhi->endpoint;
			    uhe != uhe_end;
			    uhe++) {
				ea = uhe->desc.bEndpointAddress;
				at = uhe->desc.bmAttributes;

				if (((ea & mask) == ep) &&
				    ((at & UE_XFERTYPE) == type)) {
					return (uhe);
				}
			}
		}
	}

	if ((type == UE_CONTROL) && ((ep & UE_ADDR) == 0)) {
		return (&dev->ep0);
	}
	return (NULL);
}

/*------------------------------------------------------------------------*
 *	usb_altnum_to_altsetting
 *
 * The following function returns a pointer to an alternate setting by
 * index given a "usb_interface" pointer. If the alternate setting by
 * index does not exist, NULL is returned. And alternate setting is a
 * variant of an interface, but usually with slightly different
 * characteristics.
 *------------------------------------------------------------------------*/
struct usb_host_interface *
usb_altnum_to_altsetting(const struct usb_interface *intf, uint8_t alt_index)
{
	if (alt_index >= intf->num_altsetting) {
		return (NULL);
	}
	return (intf->altsetting + alt_index);
}

/*------------------------------------------------------------------------*
 *	usb_ifnum_to_if
 *
 * The following function searches up an USB interface by
 * "bInterfaceNumber". If no match is found, NULL is returned.
 *------------------------------------------------------------------------*/
struct usb_interface *
usb_ifnum_to_if(struct usb_device *dev, uint8_t iface_no)
{
	struct usb_interface *p_ui;

	for (p_ui = dev->linux_iface_start;
	    p_ui != dev->linux_iface_end;
	    p_ui++) {
		if ((p_ui->num_altsetting > 0) &&
		    (p_ui->altsetting->desc.bInterfaceNumber == iface_no)) {
			return (p_ui);
		}
	}
	return (NULL);
}

/*------------------------------------------------------------------------*
 *	usb_buffer_alloc
 *------------------------------------------------------------------------*/
void   *
usb_buffer_alloc(struct usb_device *dev, usb_size_t size, uint16_t mem_flags, uint8_t *dma_addr)
{
	return (malloc(size, M_USBDEV, M_WAITOK | M_ZERO));
}

/*------------------------------------------------------------------------*
 *	usbd_get_intfdata
 *------------------------------------------------------------------------*/
void   *
usbd_get_intfdata(struct usb_interface *intf)
{
	return (intf->bsd_priv_sc);
}

/*------------------------------------------------------------------------*
 *	usb_linux_register
 *
 * The following function is used by the "USB_DRIVER_EXPORT()" macro,
 * and is used to register a Linux USB driver, so that its
 * "usb_device_id" structures gets searched a probe time. This
 * function is not part of the Linux USB API, and is for internal use
 * only.
 *------------------------------------------------------------------------*/
void
usb_linux_register(void *arg)
{
	struct usb_driver *drv = arg;

	mtx_lock(&Giant);
	LIST_INSERT_HEAD(&usb_linux_driver_list, drv, linux_driver_list);
	mtx_unlock(&Giant);

	usb_needs_explore_all();
}

/*------------------------------------------------------------------------*
 *	usb_linux_deregister
 *
 * The following function is used by the "USB_DRIVER_EXPORT()" macro,
 * and is used to deregister a Linux USB driver. This function will
 * ensure that all driver instances belonging to the Linux USB device
 * driver in question, gets detached before the driver is
 * unloaded. This function is not part of the Linux USB API, and is
 * for internal use only.
 *------------------------------------------------------------------------*/
void
usb_linux_deregister(void *arg)
{
	struct usb_driver *drv = arg;
	struct usb_linux_softc *sc;

repeat:
	mtx_lock(&Giant);
	LIST_FOREACH(sc, &usb_linux_attached_list, sc_attached_list) {
		if (sc->sc_udrv == drv) {
			mtx_unlock(&Giant);
			device_detach(sc->sc_fbsd_dev);
			goto repeat;
		}
	}
	LIST_REMOVE(drv, linux_driver_list);
	mtx_unlock(&Giant);
}

/*------------------------------------------------------------------------*
 *	usb_linux_free_device
 *
 * The following function is only used by the FreeBSD USB stack, to
 * cleanup and free memory after that a Linux USB device was attached.
 *------------------------------------------------------------------------*/
void
usb_linux_free_device(struct usb_device *dev)
{
	struct usb_host_endpoint *uhe;
	struct usb_host_endpoint *uhe_end;
	int err;

	uhe = dev->linux_endpoint_start;
	uhe_end = dev->linux_endpoint_end;
	while (uhe != uhe_end) {
		err = usb_setup_endpoint(dev, uhe, 0);
		uhe++;
	}
	err = usb_setup_endpoint(dev, &dev->ep0, 0);
	free(dev->linux_endpoint_start, M_USBDEV);
}

/*------------------------------------------------------------------------*
 *	usb_buffer_free
 *------------------------------------------------------------------------*/
void
usb_buffer_free(struct usb_device *dev, usb_size_t size,
    void *addr, uint8_t dma_addr)
{
	free(addr, M_USBDEV);
}

/*------------------------------------------------------------------------*
 *	usb_free_urb
 *------------------------------------------------------------------------*/
void
usb_free_urb(struct urb *urb)
{
	if (urb == NULL) {
		return;
	}
	/* make sure that the current URB is not active */
	usb_kill_urb(urb);

	/* destroy condition variable */
	cv_destroy(&urb->cv_wait);

	/* just free it */
	free(urb, M_USBDEV);
}

/*------------------------------------------------------------------------*
 *	usb_init_urb
 *
 * The following function can be used to initialize a custom URB. It
 * is not recommended to use this function. Use "usb_alloc_urb()"
 * instead.
 *------------------------------------------------------------------------*/
void
usb_init_urb(struct urb *urb)
{
	if (urb == NULL) {
		return;
	}
	memset(urb, 0, sizeof(*urb));
}

/*------------------------------------------------------------------------*
 *	usb_kill_urb
 *------------------------------------------------------------------------*/
void
usb_kill_urb(struct urb *urb)
{
	usb_unlink_urb_sub(urb, 1);
}

/*------------------------------------------------------------------------*
 *	usb_set_intfdata
 *
 * The following function sets the per Linux USB interface private
 * data pointer. It is used by most Linux USB device drivers.
 *------------------------------------------------------------------------*/
void
usb_set_intfdata(struct usb_interface *intf, void *data)
{
	intf->bsd_priv_sc = data;
}

/*------------------------------------------------------------------------*
 *	usb_linux_cleanup_interface
 *
 * The following function will release all FreeBSD USB transfers
 * associated with a Linux USB interface. It is for internal use only.
 *------------------------------------------------------------------------*/
static void
usb_linux_cleanup_interface(struct usb_device *dev, struct usb_interface *iface)
{
	struct usb_host_interface *uhi;
	struct usb_host_interface *uhi_end;
	struct usb_host_endpoint *uhe;
	struct usb_host_endpoint *uhe_end;
	int err;

	uhi = iface->altsetting;
	uhi_end = iface->altsetting + iface->num_altsetting;
	while (uhi != uhi_end) {
		uhe = uhi->endpoint;
		uhe_end = uhi->endpoint + uhi->desc.bNumEndpoints;
		while (uhe != uhe_end) {
			err = usb_setup_endpoint(dev, uhe, 0);
			uhe++;
		}
		uhi++;
	}
}

/*------------------------------------------------------------------------*
 *	usb_linux_wait_complete
 *
 * The following function is used by "usb_start_wait_urb()" to wake it
 * up, when an USB transfer has finished.
 *------------------------------------------------------------------------*/
static void
usb_linux_wait_complete(struct urb *urb)
{
	if (urb->transfer_flags & URB_IS_SLEEPING) {
		cv_signal(&urb->cv_wait);
	}
	urb->transfer_flags &= ~URB_WAIT_WAKEUP;
}

/*------------------------------------------------------------------------*
 *	usb_linux_complete
 *------------------------------------------------------------------------*/
static void
usb_linux_complete(struct usb_xfer *xfer)
{
	struct urb *urb;

	urb = usbd_xfer_get_priv(xfer);
	usbd_xfer_set_priv(xfer, NULL);
	if (urb->complete) {
		(urb->complete) (urb);
	}
}

/*------------------------------------------------------------------------*
 *	usb_linux_isoc_callback
 *
 * The following is the FreeBSD isochronous USB callback. Isochronous
 * frames are USB packets transferred 1000 or 8000 times per second,
 * depending on whether a full- or high- speed USB transfer is
 * used.
 *------------------------------------------------------------------------*/
static void
usb_linux_isoc_callback(struct usb_xfer *xfer, usb_error_t error)
{
	usb_frlength_t max_frame = xfer->max_frame_size;
	usb_frlength_t offset;
	usb_frcount_t x;
	struct urb *urb = usbd_xfer_get_priv(xfer);
	struct usb_host_endpoint *uhe = usbd_xfer_softc(xfer);
	struct usb_iso_packet_descriptor *uipd;

	DPRINTF("\n");

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		if (urb->bsd_isread) {

			/* copy in data with regard to the URB */

			offset = 0;

			for (x = 0; x < urb->number_of_packets; x++) {
				uipd = urb->iso_frame_desc + x;
				if (uipd->length > xfer->frlengths[x]) {
					if (urb->transfer_flags & URB_SHORT_NOT_OK) {
						/* XXX should be EREMOTEIO */
						uipd->status = -EPIPE;
					} else {
						uipd->status = 0;
					}
				} else {
					uipd->status = 0;
				}
				uipd->actual_length = xfer->frlengths[x];
				if (!xfer->flags.ext_buffer) {
					usbd_copy_out(xfer->frbuffers, offset,
					    USB_ADD_BYTES(urb->transfer_buffer,
					    uipd->offset), uipd->actual_length);
				}
				offset += max_frame;
			}
		} else {
			for (x = 0; x < urb->number_of_packets; x++) {
				uipd = urb->iso_frame_desc + x;
				uipd->actual_length = xfer->frlengths[x];
				uipd->status = 0;
			}
		}

		urb->actual_length = xfer->actlen;

		/* check for short transfer */
		if (xfer->actlen < xfer->sumlen) {
			/* short transfer */
			if (urb->transfer_flags & URB_SHORT_NOT_OK) {
				/* XXX should be EREMOTEIO */
				urb->status = -EPIPE;
			} else {
				urb->status = 0;
			}
		} else {
			/* success */
			urb->status = 0;
		}

		/* call callback */
		usb_linux_complete(xfer);

	case USB_ST_SETUP:
tr_setup:

		if (xfer->priv_fifo == NULL) {

			/* get next transfer */
			urb = TAILQ_FIRST(&uhe->bsd_urb_list);
			if (urb == NULL) {
				/* nothing to do */
				return;
			}
			TAILQ_REMOVE(&uhe->bsd_urb_list, urb, bsd_urb_list);
			urb->bsd_urb_list.tqe_prev = NULL;

			x = xfer->max_frame_count;
			if (urb->number_of_packets > x) {
				/* XXX simply truncate the transfer */
				urb->number_of_packets = x;
			}
		} else {
			DPRINTF("Already got a transfer\n");

			/* already got a transfer (should not happen) */
			urb = usbd_xfer_get_priv(xfer);
		}

		urb->bsd_isread = (uhe->desc.bEndpointAddress & UE_DIR_IN) ? 1 : 0;

		if (xfer->flags.ext_buffer) {
			/* set virtual address to load */
			usbd_xfer_set_frame_data(xfer, 0, urb->transfer_buffer, 0);
		}
		if (!(urb->bsd_isread)) {

			/* copy out data with regard to the URB */

			offset = 0;

			for (x = 0; x < urb->number_of_packets; x++) {
				uipd = urb->iso_frame_desc + x;
				usbd_xfer_set_frame_len(xfer, x, uipd->length);
				if (!xfer->flags.ext_buffer) {
					usbd_copy_in(xfer->frbuffers, offset,
					    USB_ADD_BYTES(urb->transfer_buffer,
					    uipd->offset), uipd->length);
				}
				offset += uipd->length;
			}
		} else {

			/*
			 * compute the transfer length into the "offset"
			 * variable
			 */

			offset = urb->number_of_packets * max_frame;

			/* setup "frlengths" array */

			for (x = 0; x < urb->number_of_packets; x++) {
				uipd = urb->iso_frame_desc + x;
				usbd_xfer_set_frame_len(xfer, x, max_frame);
			}
		}
		usbd_xfer_set_priv(xfer, urb);
		xfer->flags.force_short_xfer = 0;
		xfer->timeout = urb->timeout;
		xfer->nframes = urb->number_of_packets;
		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		if (xfer->error == USB_ERR_CANCELLED) {
			urb->status = -ECONNRESET;
		} else {
			urb->status = -EPIPE;	/* stalled */
		}

		/* Set zero for "actual_length" */
		urb->actual_length = 0;

		/* Set zero for "actual_length" */
		for (x = 0; x < urb->number_of_packets; x++) {
			urb->iso_frame_desc[x].actual_length = 0;
			urb->iso_frame_desc[x].status = urb->status;
		}

		/* call callback */
		usb_linux_complete(xfer);

		if (xfer->error == USB_ERR_CANCELLED) {
			/* we need to return in this case */
			return;
		}
		goto tr_setup;

	}
}

/*------------------------------------------------------------------------*
 *	usb_linux_non_isoc_callback
 *
 * The following is the FreeBSD BULK/INTERRUPT and CONTROL USB
 * callback. It dequeues Linux USB stack compatible URB's, transforms
 * the URB fields into a FreeBSD USB transfer, and defragments the USB
 * transfer as required. When the transfer is complete the "complete"
 * callback is called.
 *------------------------------------------------------------------------*/
static void
usb_linux_non_isoc_callback(struct usb_xfer *xfer, usb_error_t error)
{
	enum {
		REQ_SIZE = sizeof(struct usb_device_request)
	};
	struct urb *urb = usbd_xfer_get_priv(xfer);
	struct usb_host_endpoint *uhe = usbd_xfer_softc(xfer);
	uint8_t *ptr;
	usb_frlength_t max_bulk = usbd_xfer_max_len(xfer);
	uint8_t data_frame = xfer->flags_int.control_xfr ? 1 : 0;

	DPRINTF("\n");

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		if (xfer->flags_int.control_xfr) {

			/* don't transfer the setup packet again: */

			usbd_xfer_set_frame_len(xfer, 0, 0);
		}
		if (urb->bsd_isread && (!xfer->flags.ext_buffer)) {
			/* copy in data with regard to the URB */
			usbd_copy_out(xfer->frbuffers + data_frame, 0,
			    urb->bsd_data_ptr, xfer->frlengths[data_frame]);
		}
		urb->bsd_length_rem -= xfer->frlengths[data_frame];
		urb->bsd_data_ptr += xfer->frlengths[data_frame];
		urb->actual_length += xfer->frlengths[data_frame];

		/* check for short transfer */
		if (xfer->actlen < xfer->sumlen) {
			urb->bsd_length_rem = 0;

			/* short transfer */
			if (urb->transfer_flags & URB_SHORT_NOT_OK) {
				urb->status = -EPIPE;
			} else {
				urb->status = 0;
			}
		} else {
			/* check remainder */
			if (urb->bsd_length_rem > 0) {
				goto setup_bulk;
			}
			/* success */
			urb->status = 0;
		}

		/* call callback */
		usb_linux_complete(xfer);

	case USB_ST_SETUP:
tr_setup:
		/* get next transfer */
		urb = TAILQ_FIRST(&uhe->bsd_urb_list);
		if (urb == NULL) {
			/* nothing to do */
			return;
		}
		TAILQ_REMOVE(&uhe->bsd_urb_list, urb, bsd_urb_list);
		urb->bsd_urb_list.tqe_prev = NULL;

		usbd_xfer_set_priv(xfer, urb);
		xfer->flags.force_short_xfer = 0;
		xfer->timeout = urb->timeout;

		if (xfer->flags_int.control_xfr) {

			/*
			 * USB control transfers need special handling.
			 * First copy in the header, then copy in data!
			 */
			if (!xfer->flags.ext_buffer) {
				usbd_copy_in(xfer->frbuffers, 0,
				    urb->setup_packet, REQ_SIZE);
				usbd_xfer_set_frame_len(xfer, 0, REQ_SIZE);
			} else {
				/* set virtual address to load */
				usbd_xfer_set_frame_data(xfer, 0,
				    urb->setup_packet, REQ_SIZE);
			}

			ptr = urb->setup_packet;

			/* setup data transfer direction and length */
			urb->bsd_isread = (ptr[0] & UT_READ) ? 1 : 0;
			urb->bsd_length_rem = ptr[6] | (ptr[7] << 8);

		} else {

			/* setup data transfer direction */

			urb->bsd_length_rem = urb->transfer_buffer_length;
			urb->bsd_isread = (uhe->desc.bEndpointAddress &
			    UE_DIR_IN) ? 1 : 0;
		}

		urb->bsd_data_ptr = urb->transfer_buffer;
		urb->actual_length = 0;

setup_bulk:
		if (max_bulk > urb->bsd_length_rem) {
			max_bulk = urb->bsd_length_rem;
		}
		/* check if we need to force a short transfer */

		if ((max_bulk == urb->bsd_length_rem) &&
		    (urb->transfer_flags & URB_ZERO_PACKET) &&
		    (!xfer->flags_int.control_xfr)) {
			xfer->flags.force_short_xfer = 1;
		}
		/* check if we need to copy in data */

		if (xfer->flags.ext_buffer) {
			/* set virtual address to load */
			usbd_xfer_set_frame_data(xfer, data_frame,
			    urb->bsd_data_ptr, max_bulk);
		} else if (!urb->bsd_isread) {
			/* copy out data with regard to the URB */
			usbd_copy_in(xfer->frbuffers + data_frame, 0,
			    urb->bsd_data_ptr, max_bulk);
			usbd_xfer_set_frame_len(xfer, data_frame, max_bulk);
		}
		if (xfer->flags_int.control_xfr) {
			if (max_bulk > 0) {
				xfer->nframes = 2;
			} else {
				xfer->nframes = 1;
			}
		} else {
			xfer->nframes = 1;
		}
		usbd_transfer_submit(xfer);
		return;

	default:
		if (xfer->error == USB_ERR_CANCELLED) {
			urb->status = -ECONNRESET;
		} else {
			urb->status = -EPIPE;
		}

		/* Set zero for "actual_length" */
		urb->actual_length = 0;

		/* call callback */
		usb_linux_complete(xfer);

		if (xfer->error == USB_ERR_CANCELLED) {
			/* we need to return in this case */
			return;
		}
		goto tr_setup;
	}
}

/*------------------------------------------------------------------------*
 *	usb_fill_bulk_urb
 *------------------------------------------------------------------------*/
void
usb_fill_bulk_urb(struct urb *urb, struct usb_device *udev,
    struct usb_host_endpoint *uhe, void *buf,
    int length, usb_complete_t callback, void *arg)
{
	urb->dev = udev;
	urb->endpoint = uhe;
	urb->transfer_buffer = buf;
	urb->transfer_buffer_length = length;
	urb->complete = callback;
	urb->context = arg;
}

/*------------------------------------------------------------------------*
 *	usb_bulk_msg
 *
 * NOTE: This function can also be used for interrupt endpoints!
 *
 * Return values:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
int
usb_bulk_msg(struct usb_device *udev, struct usb_host_endpoint *uhe,
    void *data, int len, uint16_t *pactlen, usb_timeout_t timeout)
{
	struct urb *urb;
	int err;

	if (uhe == NULL)
		return (-EINVAL);
	if (len < 0)
		return (-EINVAL);

	err = usb_setup_endpoint(udev, uhe, 4096 /* bytes */);
	if (err)
		return (err);

	urb = usb_alloc_urb(0, 0);
	if (urb == NULL)
		return (-ENOMEM);

	usb_fill_bulk_urb(urb, udev, uhe, data, len,
	    usb_linux_wait_complete, NULL);

	err = usb_start_wait_urb(urb, timeout, pactlen);

	usb_free_urb(urb);

	return (err);
}
MODULE_DEPEND(linuxkpi, usb, 1, 1, 1);

static void
usb_linux_init(void *arg)
{
	/* register our function */
	usb_linux_free_device_p = &usb_linux_free_device;
}
SYSINIT(usb_linux_init, SI_SUB_LOCK, SI_ORDER_FIRST, usb_linux_init, NULL);
SYSUNINIT(usb_linux_unload, SI_SUB_LOCK, SI_ORDER_ANY, usb_linux_unload, NULL);
