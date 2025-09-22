/*	$OpenBSD: usbdi.c,v 1.112 2025/04/03 11:02:44 kirill Exp $ */
/*	$NetBSD: usbdi.c,v 1.103 2002/09/27 15:37:38 provos Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/usbdi.c,v 1.28 1999/11/17 22:33:49 n_hibma Exp $	*/

/*
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#ifdef USB_DEBUG
#define DPRINTF(x)	do { if (usbdebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (usbdebug>(n)) printf x; } while (0)
extern int usbdebug;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

void usbd_request_async_cb(struct usbd_xfer *, void *, usbd_status);
void usbd_start_next(struct usbd_pipe *pipe);
usbd_status usbd_open_pipe_ival(struct usbd_interface *, u_int8_t, u_int8_t,
    struct usbd_pipe **, int);

int
usbd_is_dying(struct usbd_device *dev)
{
	return (dev->dying || dev->bus->dying);
}

void
usbd_deactivate(struct usbd_device *dev)
{
	dev->dying = 1;
}

void
usbd_ref_incr(struct usbd_device *dev)
{
	dev->ref_cnt++;
}

void
usbd_ref_decr(struct usbd_device *dev)
{
	if (--dev->ref_cnt == 0)
		wakeup(&dev->ref_cnt);
}

void
usbd_ref_wait(struct usbd_device *dev)
{
	while (dev->ref_cnt > 0)
		tsleep_nsec(&dev->ref_cnt, PWAIT, "usbref", SEC_TO_NSEC(60));
}

int
usbd_get_devcnt(struct usbd_device *dev)
{
	return (dev->ndevs);
}

void
usbd_claim_iface(struct usbd_device *dev, int ifaceno)
{
	dev->ifaces[ifaceno].claimed = 1;
}

int
usbd_iface_claimed(struct usbd_device *dev, int ifaceno)
{
	return (dev->ifaces[ifaceno].claimed);
}

#ifdef USB_DEBUG
void
usbd_dump_iface(struct usbd_interface *iface)
{
	printf("%s: iface=%p\n", __func__, iface);
	if (iface == NULL)
		return;
	printf(" device=%p idesc=%p index=%d altindex=%d priv=%p\n",
	    iface->device, iface->idesc, iface->index, iface->altindex,
	    iface->priv);
}

void
usbd_dump_device(struct usbd_device *dev)
{
	printf("%s: dev=%p\n", __func__, dev);
	if (dev == NULL)
		return;
	printf(" bus=%p default_pipe=%p\n", dev->bus, dev->default_pipe);
	printf(" address=%d config=%d depth=%d speed=%d self_powered=%d "
	    "power=%d langid=%d\n", dev->address, dev->config, dev->depth,
	    dev->speed, dev->self_powered, dev->power, dev->langid);
}

void
usbd_dump_endpoint(struct usbd_endpoint *endp)
{
	printf("%s: endp=%p\n", __func__, endp);
	if (endp == NULL)
		return;
	printf(" edesc=%p refcnt=%d\n", endp->edesc, endp->refcnt);
	if (endp->edesc)
		printf(" bEndpointAddress=0x%02x\n",
		    endp->edesc->bEndpointAddress);
}

void
usbd_dump_queue(struct usbd_pipe *pipe)
{
	struct usbd_xfer *xfer;

	printf("%s: pipe=%p\n", __func__, pipe);
	SIMPLEQ_FOREACH(xfer, &pipe->queue, next) {
		printf("  xfer=%p\n", xfer);
	}
}

void
usbd_dump_pipe(struct usbd_pipe *pipe)
{
	printf("%s: pipe=%p\n", __func__, pipe);
	if (pipe == NULL)
		return;
	usbd_dump_iface(pipe->iface);
	usbd_dump_device(pipe->device);
	usbd_dump_endpoint(pipe->endpoint);
	printf(" (usbd_dump_pipe:)\n running=%d aborting=%d\n",
	    pipe->running, pipe->aborting);
	printf(" intrxfer=%p, repeat=%d, interval=%d\n", pipe->intrxfer,
	    pipe->repeat, pipe->interval);
}
#endif

usbd_status
usbd_open_pipe(struct usbd_interface *iface, u_int8_t address, u_int8_t flags,
    struct usbd_pipe **pipe)
{
	return (usbd_open_pipe_ival(iface, address, flags, pipe,
	    USBD_DEFAULT_INTERVAL));
}

usbd_status
usbd_open_pipe_ival(struct usbd_interface *iface, u_int8_t address,
    u_int8_t flags, struct usbd_pipe **pipe, int ival)
{
	struct usbd_pipe *p;
	struct usbd_endpoint *ep;
	usbd_status err;
	int i;

	DPRINTFN(3,("%s: iface=%p address=0x%x flags=0x%x\n", __func__,
	    iface, address, flags));

	for (i = 0; i < iface->idesc->bNumEndpoints; i++) {
		ep = &iface->endpoints[i];
		if (ep->edesc == NULL)
			return (USBD_IOERROR);
		if (ep->edesc->bEndpointAddress == address)
			goto found;
	}
	return (USBD_BAD_ADDRESS);
 found:
	if ((flags & USBD_EXCLUSIVE_USE) && ep->refcnt != 0)
		return (USBD_IN_USE);
	err = usbd_setup_pipe(iface->device, iface, ep, ival, &p);
	if (err)
		return (err);
	LIST_INSERT_HEAD(&iface->pipes, p, next);
	*pipe = p;
	return (USBD_NORMAL_COMPLETION);
}

usbd_status
usbd_open_pipe_intr(struct usbd_interface *iface, u_int8_t address,
    u_int8_t flags, struct usbd_pipe **pipe, void *priv,
    void *buffer, u_int32_t len, usbd_callback cb, int ival)
{
	usbd_status err;
	struct usbd_xfer *xfer;
	struct usbd_pipe *ipipe;

	DPRINTFN(3,("%s: address=0x%x flags=0x%x len=%d\n", __func__,
	    address, flags, len));

	err = usbd_open_pipe_ival(iface, address, USBD_EXCLUSIVE_USE, &ipipe,
	    ival);
	if (err)
		return (err);
	xfer = usbd_alloc_xfer(iface->device);
	if (xfer == NULL) {
		err = USBD_NOMEM;
		goto bad1;
	}
	usbd_setup_xfer(xfer, ipipe, priv, buffer, len, flags,
	    USBD_NO_TIMEOUT, cb);
	ipipe->intrxfer = xfer;
	ipipe->repeat = 1;
	err = usbd_transfer(xfer);
	*pipe = ipipe;
	if (err != USBD_IN_PROGRESS)
		goto bad2;
	return (USBD_NORMAL_COMPLETION);

 bad2:
	ipipe->intrxfer = NULL;
	ipipe->repeat = 0;
	usbd_free_xfer(xfer);
 bad1:
	usbd_close_pipe(ipipe);
	return (err);
}

usbd_status
usbd_close_pipe(struct usbd_pipe *pipe)
{
#ifdef DIAGNOSTIC
	if (pipe == NULL) {
		printf("usbd_close_pipe: pipe==NULL\n");
		return (USBD_NORMAL_COMPLETION);
	}
#endif

	if (!SIMPLEQ_EMPTY(&pipe->queue))
		usbd_abort_pipe(pipe);

	/* Default pipes are never linked */
	if (pipe->iface != NULL)
		LIST_REMOVE(pipe, next);
	pipe->endpoint->refcnt--;
	pipe->methods->close(pipe);
	if (pipe->intrxfer != NULL)
		usbd_free_xfer(pipe->intrxfer);
	free(pipe, M_USB, pipe->pipe_size);
	return (USBD_NORMAL_COMPLETION);
}

usbd_status
usbd_transfer(struct usbd_xfer *xfer)
{
	struct usbd_pipe *pipe = xfer->pipe;
	struct usbd_bus *bus = pipe->device->bus;
	int polling = bus->use_polling;
	usbd_status err;
	int flags, s;

	if (usbd_is_dying(pipe->device))
		return (USBD_IOERROR);

	DPRINTFN(5,("%s: xfer=%p, flags=%d, pipe=%p, running=%d\n", __func__,
	    xfer, xfer->flags, pipe, pipe->running));
#ifdef USB_DEBUG
	if (usbdebug > 5)
		usbd_dump_queue(pipe);
#endif
	xfer->done = 0;
	xfer->status = USBD_NOT_STARTED;

	if (pipe->aborting)
		return (USBD_CANCELLED);

	/* If there is no buffer, allocate one. */
	if ((xfer->rqflags & URQ_DEV_DMABUF) == 0) {
#ifdef DIAGNOSTIC
		if (xfer->rqflags & URQ_AUTO_DMABUF)
			printf("usbd_transfer: has old buffer!\n");
#endif
		err = usb_allocmem(bus, xfer->length, 0, 0, &xfer->dmabuf);
		if (err)
			return (err);
		xfer->rqflags |= URQ_AUTO_DMABUF;
	}

	if (!usbd_xfer_isread(xfer) && (xfer->flags & USBD_NO_COPY) == 0)
		memcpy(KERNADDR(&xfer->dmabuf, 0), xfer->buffer,
		    xfer->length);

	usb_tap(bus, xfer, USBTAP_DIR_OUT);

	err = pipe->methods->transfer(xfer);

	if (err != USBD_IN_PROGRESS && err != USBD_NORMAL_COMPLETION) {
		/* The transfer has not been queued, so free buffer. */
		if (xfer->rqflags & URQ_AUTO_DMABUF) {
			usb_freemem(bus, &xfer->dmabuf);
			xfer->rqflags &= ~URQ_AUTO_DMABUF;
		}
	}

	if (!(xfer->flags & USBD_SYNCHRONOUS))
		return (err);

	/* Sync transfer, wait for completion. */
	if (err != USBD_IN_PROGRESS)
		return (err);

	s = splusb();
	if (polling) {
		int timo;

		for (timo = xfer->timeout; timo >= 0; timo--) {
			usb_delay_ms(bus, 1);
			if (bus->dying) {
				xfer->status = USBD_IOERROR;
				usb_transfer_complete(xfer);
				break;
			}

			usbd_dopoll(pipe->device);
			if (xfer->done)
				break;
		}

		if (timo < 0) {
			xfer->status = USBD_TIMEOUT;
			usb_transfer_complete(xfer);
		}
	} else {
		while (!xfer->done) {
			flags = PRIBIO|(xfer->flags & USBD_CATCH ? PCATCH : 0);

			err = tsleep_nsec(xfer, flags, "usbsyn", INFSLP);
			if (err && !xfer->done) {
				usbd_abort_pipe(pipe);
				if (err == EINTR)
					xfer->status = USBD_INTERRUPTED;
				else
					xfer->status = USBD_TIMEOUT;
			}
		}
	}
	splx(s);
	return (xfer->status);
}

void *
usbd_alloc_buffer(struct usbd_xfer *xfer, u_int32_t size)
{
	struct usbd_bus *bus = xfer->device->bus;
	usbd_status err;

#ifdef DIAGNOSTIC
	if (xfer->rqflags & (URQ_DEV_DMABUF | URQ_AUTO_DMABUF))
		printf("usbd_alloc_buffer: xfer already has a buffer\n");
#endif
	err = usb_allocmem(bus, size, 0, 0, &xfer->dmabuf);
	if (err)
		return (NULL);
	xfer->rqflags |= URQ_DEV_DMABUF;
	return (KERNADDR(&xfer->dmabuf, 0));
}

void
usbd_free_buffer(struct usbd_xfer *xfer)
{
#ifdef DIAGNOSTIC
	if (!(xfer->rqflags & (URQ_DEV_DMABUF | URQ_AUTO_DMABUF))) {
		printf("usbd_free_buffer: no buffer\n");
		return;
	}
#endif
	xfer->rqflags &= ~(URQ_DEV_DMABUF | URQ_AUTO_DMABUF);
	usb_freemem(xfer->device->bus, &xfer->dmabuf);
}

struct usbd_xfer *
usbd_alloc_xfer(struct usbd_device *dev)
{
	struct usbd_xfer *xfer;

	xfer = dev->bus->methods->allocx(dev->bus);
	if (xfer == NULL)
		return (NULL);
#ifdef DIAGNOSTIC
	xfer->busy_free = XFER_FREE;
#endif
	xfer->device = dev;
	timeout_set(&xfer->timeout_handle, NULL, NULL);
	DPRINTFN(5,("usbd_alloc_xfer() = %p\n", xfer));
	return (xfer);
}

void
usbd_free_xfer(struct usbd_xfer *xfer)
{
	DPRINTFN(5,("%s: %p\n", __func__, xfer));
	if (xfer->rqflags & (URQ_DEV_DMABUF | URQ_AUTO_DMABUF))
		usbd_free_buffer(xfer);
#ifdef DIAGNOSTIC
	if (xfer->busy_free != XFER_FREE) {
		printf("%s: xfer=%p not free\n", __func__, xfer);
		return;
	}
#endif
	xfer->device->bus->methods->freex(xfer->device->bus, xfer);
}

void
usbd_setup_xfer(struct usbd_xfer *xfer, struct usbd_pipe *pipe,
    void *priv, void *buffer, u_int32_t length, u_int16_t flags,
    u_int32_t timeout, usbd_callback callback)
{
	xfer->pipe = pipe;
	xfer->priv = priv;
	xfer->buffer = buffer;
	xfer->length = length;
	xfer->actlen = 0;
	xfer->flags = flags;
	xfer->timeout = timeout;
	xfer->status = USBD_NOT_STARTED;
	xfer->callback = callback;
	xfer->rqflags &= ~URQ_REQUEST;
	xfer->nframes = 0;
}

void
usbd_setup_default_xfer(struct usbd_xfer *xfer, struct usbd_device *dev,
    void *priv, u_int32_t timeout, usb_device_request_t *req,
    void *buffer, u_int32_t length, u_int16_t flags, usbd_callback callback)
{
	xfer->pipe = dev->default_pipe;
	xfer->priv = priv;
	xfer->buffer = buffer;
	xfer->length = length;
	xfer->actlen = 0;
	xfer->flags = flags;
	xfer->timeout = timeout;
	xfer->status = USBD_NOT_STARTED;
	xfer->callback = callback;
	xfer->request = *req;
	xfer->rqflags |= URQ_REQUEST;
	xfer->nframes = 0;
}

void
usbd_setup_isoc_xfer(struct usbd_xfer *xfer, struct usbd_pipe *pipe,
    void *priv, u_int16_t *frlengths, u_int32_t nframes,
    u_int16_t flags, usbd_callback callback)
{
	int i;

	xfer->pipe = pipe;
	xfer->priv = priv;
	xfer->buffer = 0;
	xfer->length = 0;
	for (i = 0; i < nframes; i++)
		xfer->length += frlengths[i];
	xfer->actlen = 0;
	xfer->flags = flags;
	xfer->timeout = USBD_NO_TIMEOUT;
	xfer->status = USBD_NOT_STARTED;
	xfer->callback = callback;
	xfer->rqflags &= ~URQ_REQUEST;
	xfer->frlengths = frlengths;
	xfer->nframes = nframes;
}

void
usbd_get_xfer_status(struct usbd_xfer *xfer, void **priv,
    void **buffer, u_int32_t *count, usbd_status *status)
{
	if (priv != NULL)
		*priv = xfer->priv;
	if (buffer != NULL)
		*buffer = xfer->buffer;
	if (count != NULL)
		*count = xfer->actlen;
	if (status != NULL)
		*status = xfer->status;
}

usb_config_descriptor_t *
usbd_get_config_descriptor(struct usbd_device *dev)
{
#ifdef DIAGNOSTIC
	if (dev == NULL) {
		printf("usbd_get_config_descriptor: dev == NULL\n");
		return (NULL);
	}
#endif
	return (dev->cdesc);
}

usb_interface_descriptor_t *
usbd_get_interface_descriptor(struct usbd_interface *iface)
{
#ifdef DIAGNOSTIC
	if (iface == NULL) {
		printf("usbd_get_interface_descriptor: dev == NULL\n");
		return (NULL);
	}
#endif
	return (iface->idesc);
}

usb_device_descriptor_t *
usbd_get_device_descriptor(struct usbd_device *dev)
{
	return (&dev->ddesc);
}

usb_endpoint_descriptor_t *
usbd_interface2endpoint_descriptor(struct usbd_interface *iface, u_int8_t index)
{
	if (index >= iface->idesc->bNumEndpoints)
		return (0);
	return (iface->endpoints[index].edesc);
}

void
usbd_abort_pipe(struct usbd_pipe *pipe)
{
	struct usbd_xfer *xfer;
	int s;

#ifdef DIAGNOSTIC
	if (pipe == NULL) {
		printf("usbd_abort_pipe: pipe==NULL\n");
		return;
	}
#endif
	s = splusb();
	DPRINTFN(2,("%s: pipe=%p\n", __func__, pipe));
#ifdef USB_DEBUG
	if (usbdebug > 5)
		usbd_dump_queue(pipe);
#endif
	pipe->repeat = 0;
	pipe->aborting = 1;
	while ((xfer = SIMPLEQ_FIRST(&pipe->queue)) != NULL) {
		DPRINTFN(2,("%s: pipe=%p xfer=%p (methods=%p)\n", __func__,
		    pipe, xfer, pipe->methods));
		/* Make the HC abort it (and invoke the callback). */
		pipe->methods->abort(xfer);
		/* XXX only for non-0 usbd_clear_endpoint_stall(pipe); */
	}
	pipe->aborting = 0;
	splx(s);
}

usbd_status
usbd_clear_endpoint_stall(struct usbd_pipe *pipe)
{
	struct usbd_device *dev = pipe->device;
	usb_device_request_t req;
	usbd_status err;

	DPRINTFN(8, ("usbd_clear_endpoint_stall\n"));

	/*
	 * Clearing en endpoint stall resets the endpoint toggle, so
	 * do the same to the HC toggle.
	 */
	usbd_clear_endpoint_toggle(pipe);

	req.bmRequestType = UT_WRITE_ENDPOINT;
	req.bRequest = UR_CLEAR_FEATURE;
	USETW(req.wValue, UF_ENDPOINT_HALT);
	USETW(req.wIndex, pipe->endpoint->edesc->bEndpointAddress);
	USETW(req.wLength, 0);
	err = usbd_do_request(dev, &req, 0);

	return (err);
}

usbd_status
usbd_clear_endpoint_stall_async(struct usbd_pipe *pipe)
{
	struct usbd_device *dev = pipe->device;
	struct usbd_xfer *xfer;
	usb_device_request_t req;
	usbd_status err;

	usbd_clear_endpoint_toggle(pipe);

	req.bmRequestType = UT_WRITE_ENDPOINT;
	req.bRequest = UR_CLEAR_FEATURE;
	USETW(req.wValue, UF_ENDPOINT_HALT);
	USETW(req.wIndex, pipe->endpoint->edesc->bEndpointAddress);
	USETW(req.wLength, 0);

	xfer = usbd_alloc_xfer(dev);
	if (xfer == NULL)
		return (USBD_NOMEM);

	err = usbd_request_async(xfer, &req, NULL, NULL);
	return (err);
}

void
usbd_clear_endpoint_toggle(struct usbd_pipe *pipe)
{
	if (pipe->methods->cleartoggle != NULL)
		pipe->methods->cleartoggle(pipe);
}

usbd_status
usbd_device2interface_handle(struct usbd_device *dev, u_int8_t ifaceno,
    struct usbd_interface **iface)
{
	u_int8_t idx;

	if (dev->cdesc == NULL)
		return (USBD_NOT_CONFIGURED);
	if (ifaceno < dev->cdesc->bNumInterfaces) {
		*iface = &dev->ifaces[ifaceno];
		return (USBD_NORMAL_COMPLETION);
	}
	/*
	 * The correct interface should be at dev->ifaces[ifaceno], but we've
	 * seen non-compliant devices in the wild which present non-contiguous
	 * interface numbers and this skews the indices. For this reason we
	 * linearly search the interface array.
	 */
	for (idx = 0; idx < dev->cdesc->bNumInterfaces; idx++) {
		if (dev->ifaces[idx].idesc->bInterfaceNumber == ifaceno) {
			*iface = &dev->ifaces[idx];
			return (USBD_NORMAL_COMPLETION);
		}
	}
	return (USBD_INVAL);
}

/* XXXX use altno */
usbd_status
usbd_set_interface(struct usbd_interface *iface, int altno)
{
	usb_device_request_t req;
	usbd_status err;
	struct usbd_endpoint *endpoints;
	int nendpt;

	if (LIST_FIRST(&iface->pipes) != 0)
		return (USBD_IN_USE);

	endpoints = iface->endpoints;
	nendpt = iface->nendpt;
	err = usbd_fill_iface_data(iface->device, iface->index, altno);
	if (err)
		return (err);

	/* new setting works, we can free old endpoints */
	free(endpoints, M_USB, nendpt * sizeof(*endpoints));

#ifdef DIAGNOSTIC
	if (iface->idesc == NULL) {
		printf("usbd_set_interface: NULL pointer\n");
		return (USBD_INVAL);
	}
#endif

	req.bmRequestType = UT_WRITE_INTERFACE;
	req.bRequest = UR_SET_INTERFACE;
	USETW(req.wValue, iface->idesc->bAlternateSetting);
	USETW(req.wIndex, iface->idesc->bInterfaceNumber);
	USETW(req.wLength, 0);
	return (usbd_do_request(iface->device, &req, 0));
}

int
usbd_get_no_alts(usb_config_descriptor_t *cdesc, int ifaceno)
{
	char *p = (char *)cdesc;
	char *end = p + UGETW(cdesc->wTotalLength);
	usb_interface_descriptor_t *d;
	int n;

	for (n = 0; p < end; p += d->bLength) {
		d = (usb_interface_descriptor_t *)p;
		if (p + d->bLength <= end &&
		    d->bDescriptorType == UDESC_INTERFACE &&
		    d->bInterfaceNumber == ifaceno)
			n++;
	}
	return (n);
}

int
usbd_get_interface_altindex(struct usbd_interface *iface)
{
	return (iface->altindex);
}

/*** Internal routines ***/

/* Called at splusb() */
void
usb_transfer_complete(struct usbd_xfer *xfer)
{
	struct usbd_pipe *pipe = xfer->pipe;
	struct usbd_bus *bus = pipe->device->bus;
	int polling = bus->use_polling;
	int status, flags;

#if 0
	/* XXX ohci_intr1() calls usb_transfer_complete() for RHSC. */
	splsoftassert(IPL_SOFTUSB);
#endif

	DPRINTFN(5, ("usb_transfer_complete: pipe=%p xfer=%p status=%d "
		     "actlen=%d\n", pipe, xfer, xfer->status, xfer->actlen));
#ifdef DIAGNOSTIC
	if (xfer->busy_free != XFER_ONQU) {
		printf("%s: xfer=%p not on queue\n", __func__, xfer);
		return;
	}
#endif

	/* XXXX */
	if (polling)
		pipe->running = 0;

	if (xfer->actlen > xfer->length) {
#ifdef DIAGNOSTIC
		printf("%s: actlen > len %u > %u\n", __func__, xfer->actlen,
		    xfer->length);
#endif
		xfer->actlen = xfer->length;
	}

	if (usbd_xfer_isread(xfer) && xfer->actlen != 0 &&
	    (xfer->flags & USBD_NO_COPY) == 0)
		memcpy(xfer->buffer, KERNADDR(&xfer->dmabuf, 0),
		    xfer->actlen);

	/* if we allocated the buffer in usbd_transfer() we free it here. */
	if (xfer->rqflags & URQ_AUTO_DMABUF) {
		if (!pipe->repeat) {
			usb_freemem(bus, &xfer->dmabuf);
			xfer->rqflags &= ~URQ_AUTO_DMABUF;
		}
	}

	if (!pipe->repeat) {
		/* Remove request from queue. */
		KASSERT(xfer == SIMPLEQ_FIRST(&pipe->queue));
		SIMPLEQ_REMOVE_HEAD(&pipe->queue, next);
#ifdef DIAGNOSTIC
		xfer->busy_free = XFER_FREE;
#endif
	}
	DPRINTFN(5,("%s: repeat=%d new head=%p\n", __func__,
	    pipe->repeat, SIMPLEQ_FIRST(&pipe->queue)));

	/* Count completed transfers. */
	++bus->stats.uds_requests
		[UE_GET_XFERTYPE(pipe->endpoint->edesc->bmAttributes)];

	xfer->done = 1;
	if (!xfer->status && xfer->actlen < xfer->length &&
	    !(xfer->flags & USBD_SHORT_XFER_OK)) {
		DPRINTFN(-1,("%s: short transfer %d<%d\n", __func__,
		    xfer->actlen, xfer->length));
		xfer->status = USBD_SHORT_XFER;
	}

	usb_tap(bus, xfer, USBTAP_DIR_IN);

	/*
	 * We cannot dereference ``xfer'' after calling the callback as
	 * it might free it.
	 */
	status = xfer->status;
	flags = xfer->flags;

	if (pipe->repeat) {
		if (xfer->callback)
			xfer->callback(xfer, xfer->priv, xfer->status);
		pipe->methods->done(xfer);
	} else {
		pipe->methods->done(xfer);
		if (xfer->callback)
			xfer->callback(xfer, xfer->priv, xfer->status);
	}

	if ((flags & USBD_SYNCHRONOUS) && !polling)
		wakeup(xfer);

	if (!pipe->repeat) {
		/* XXX should we stop the queue on all errors? */
		if ((status == USBD_CANCELLED || status == USBD_IOERROR ||
		     status == USBD_TIMEOUT) &&
		    pipe->iface != NULL)		/* not control pipe */
			pipe->running = 0;
		else
			usbd_start_next(pipe);
	}
}

usbd_status
usb_insert_transfer(struct usbd_xfer *xfer)
{
	struct usbd_pipe *pipe = xfer->pipe;
	usbd_status err;
	int s;

	DPRINTFN(5,("%s: pipe=%p running=%d timeout=%d\n", __func__,
	    pipe, pipe->running, xfer->timeout));
#ifdef DIAGNOSTIC
	if (xfer->busy_free != XFER_FREE) {
		printf("%s: xfer=%p not free\n", __func__, xfer);
		return (USBD_INVAL);
	}
	xfer->busy_free = XFER_ONQU;
#endif
	s = splusb();
	SIMPLEQ_INSERT_TAIL(&pipe->queue, xfer, next);
	if (pipe->running)
		err = USBD_IN_PROGRESS;
	else {
		pipe->running = 1;
		err = USBD_NORMAL_COMPLETION;
	}
	splx(s);
	return (err);
}

/* Called at splusb() */
void
usbd_start_next(struct usbd_pipe *pipe)
{
	struct usbd_xfer *xfer;
	usbd_status err;

	splsoftassert(IPL_SOFTUSB);

#ifdef DIAGNOSTIC
	if (pipe == NULL) {
		printf("usbd_start_next: pipe == NULL\n");
		return;
	}
	if (pipe->methods == NULL || pipe->methods->start == NULL) {
		printf("%s: pipe=%p no start method\n", __func__, pipe);
		return;
	}
#endif

	/* Get next request in queue. */
	xfer = SIMPLEQ_FIRST(&pipe->queue);
	DPRINTFN(5, ("%s: pipe=%p, xfer=%p\n", __func__, pipe, xfer));
	if (xfer == NULL) {
		pipe->running = 0;
	} else {
		err = pipe->methods->start(xfer);
		if (err != USBD_IN_PROGRESS) {
			printf("%s: error=%d\n", __func__, err);
			pipe->running = 0;
			/* XXX do what? */
		}
	}
}

usbd_status
usbd_do_request(struct usbd_device *dev, usb_device_request_t *req, void *data)
{
	return (usbd_do_request_flags(dev, req, data, 0, 0,
	    USBD_DEFAULT_TIMEOUT));
}

usbd_status
usbd_do_request_flags(struct usbd_device *dev, usb_device_request_t *req,
    void *data, uint16_t flags, int *actlen, uint32_t timeout)
{
	struct usbd_xfer *xfer;
	usbd_status err;

#ifdef DIAGNOSTIC
	if (dev->bus->intr_context) {
		printf("usbd_do_request: not in process context\n");
		return (USBD_INVAL);
	}
#endif

	/* If the bus is gone, don't go any further. */
	if (usbd_is_dying(dev))
		return (USBD_IOERROR);

	xfer = usbd_alloc_xfer(dev);
	if (xfer == NULL)
		return (USBD_NOMEM);
	usbd_setup_default_xfer(xfer, dev, 0, timeout, req, data,
	    UGETW(req->wLength), flags | USBD_SYNCHRONOUS, 0);
	err = usbd_transfer(xfer);
	if (actlen != NULL)
		*actlen = xfer->actlen;
	if (err == USBD_STALLED) {
		/*
		 * The control endpoint has stalled.  Control endpoints
		 * should not halt, but some may do so anyway so clear
		 * any halt condition.
		 */
		usb_device_request_t treq;
		usb_status_t status;
		u_int16_t s;
		usbd_status nerr;

		treq.bmRequestType = UT_READ_ENDPOINT;
		treq.bRequest = UR_GET_STATUS;
		USETW(treq.wValue, 0);
		USETW(treq.wIndex, 0);
		USETW(treq.wLength, sizeof(usb_status_t));
		usbd_setup_default_xfer(xfer, dev, 0, USBD_DEFAULT_TIMEOUT,
		    &treq, &status, sizeof(usb_status_t), USBD_SYNCHRONOUS, 0);
		nerr = usbd_transfer(xfer);
		if (nerr)
			goto bad;
		s = UGETW(status.wStatus);
		DPRINTF(("%s: status = 0x%04x\n", __func__, s));
		if (!(s & UES_HALT))
			goto bad;
		treq.bmRequestType = UT_WRITE_ENDPOINT;
		treq.bRequest = UR_CLEAR_FEATURE;
		USETW(treq.wValue, UF_ENDPOINT_HALT);
		USETW(treq.wIndex, 0);
		USETW(treq.wLength, 0);
		usbd_setup_default_xfer(xfer, dev, 0, USBD_DEFAULT_TIMEOUT,
		    &treq, &status, 0, USBD_SYNCHRONOUS, 0);
		nerr = usbd_transfer(xfer);
		if (nerr)
			goto bad;
	}

 bad:
	usbd_free_xfer(xfer);
	return (err);
}

void
usbd_request_async_cb(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	usbd_free_xfer(xfer);
}

/*
 * Execute a request without waiting for completion.
 * Can be used from interrupt context.
 */
usbd_status
usbd_request_async(struct usbd_xfer *xfer, usb_device_request_t *req,
    void *priv, usbd_callback callback)
{
	usbd_status err;

	if (callback == NULL)
		callback = usbd_request_async_cb;

	usbd_setup_default_xfer(xfer, xfer->device, priv,
	    USBD_DEFAULT_TIMEOUT, req, NULL, UGETW(req->wLength),
	    USBD_NO_COPY, callback);
	err = usbd_transfer(xfer);
	if (err != USBD_IN_PROGRESS) {
		usbd_free_xfer(xfer);
		return (err);
	}
	return (USBD_NORMAL_COMPLETION);
}

const struct usbd_quirks *
usbd_get_quirks(struct usbd_device *dev)
{
#ifdef DIAGNOSTIC
	if (dev == NULL) {
		printf("usbd_get_quirks: dev == NULL\n");
		return 0;
	}
#endif
	return (dev->quirks);
}

/* XXX do periodic free() of free list */

/*
 * Called from keyboard driver when in polling mode.
 */
void
usbd_dopoll(struct usbd_device *udev)
{
	udev->bus->methods->do_poll(udev->bus);
}

void
usbd_set_polling(struct usbd_device *dev, int on)
{
	if (on)
		dev->bus->use_polling++;
	else
		dev->bus->use_polling--;
	/* When polling we need to make sure there is nothing pending to do. */
	if (dev->bus->use_polling)
		dev->bus->methods->soft_intr(dev->bus);
}

usb_endpoint_descriptor_t *
usbd_get_endpoint_descriptor(struct usbd_interface *iface, u_int8_t address)
{
	struct usbd_endpoint *ep;
	int i;

	for (i = 0; i < iface->idesc->bNumEndpoints; i++) {
		ep = &iface->endpoints[i];
		if (ep->edesc->bEndpointAddress == address)
			return (iface->endpoints[i].edesc);
	}
	return (0);
}

/*
 * usbd_ratecheck() can limit the number of error messages that occurs.
 * When a device is unplugged it may take up to 0.25s for the hub driver
 * to notice it.  If the driver continuously tries to do I/O operations
 * this can generate a large number of messages.
 */
int
usbd_ratecheck(struct timeval *last)
{
	static struct timeval errinterval = { 0, 250000 }; /* 0.25 s*/

	return (ratecheck(last, &errinterval));
}

/*
 * Search for a vendor/product pair in an array.  The item size is
 * given as an argument.
 */
const struct usb_devno *
usbd_match_device(const struct usb_devno *tbl, u_int nentries, u_int sz,
    u_int16_t vendor, u_int16_t product)
{
	while (nentries-- > 0) {
		u_int16_t tproduct = tbl->ud_product;
		if (tbl->ud_vendor == vendor &&
		    (tproduct == product || tproduct == USB_PRODUCT_ANY))
			return (tbl);
		tbl = (const struct usb_devno *)((const char *)tbl + sz);
	}
	return (NULL);
}

void
usbd_desc_iter_init(struct usbd_device *dev, struct usbd_desc_iter *iter)
{
	const usb_config_descriptor_t *cd = usbd_get_config_descriptor(dev);

	iter->cur = (const uByte *)cd;
	iter->end = (const uByte *)cd + UGETW(cd->wTotalLength);
}

const usb_descriptor_t *
usbd_desc_iter_next(struct usbd_desc_iter *iter)
{
	const usb_descriptor_t *desc;

	if (iter->cur + sizeof(usb_descriptor_t) >= iter->end) {
		if (iter->cur != iter->end)
			printf("usbd_desc_iter_next: bad descriptor\n");
		return NULL;
	}
	desc = (const usb_descriptor_t *)iter->cur;
	if (desc->bLength == 0) {
		printf("usbd_desc_iter_next: descriptor length = 0\n");
		return NULL;
	}
	iter->cur += desc->bLength;
	if (iter->cur > iter->end) {
		printf("usbd_desc_iter_next: descriptor length too large\n");
		return NULL;
	}
	return desc;
}

int
usbd_str(usb_string_descriptor_t *p, int l, const char *s)
{
	int i;

	if (l == 0)
		return (0);
	p->bLength = 2 * strlen(s) + 2;
	if (l == 1)
		return (1);
	p->bDescriptorType = UDESC_STRING;
	l -= 2;
	for (i = 0; s[i] && l > 1; i++, l -= 2)
		USETW2(p->bString[i], 0, s[i]);
	return (2 * i + 2);
}
