/*	$OpenBSD: dwc2.c,v 1.68 2022/09/18 21:12:19 mglocker Exp $	*/
/*	$NetBSD: dwc2.c,v 1.32 2014/09/02 23:26:20 macallan Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nick Hudson
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
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/endian.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#include <dev/usb/dwc2/dwc2.h>
#include <dev/usb/dwc2/dwc2var.h>

#include <dev/usb/dwc2/dwc2_core.h>
#include <dev/usb/dwc2/dwc2_hcd.h>

#ifdef DWC2_COUNTERS
#define	DWC2_EVCNT_ADD(a,b)	((void)((a).ev_count += (b)))
#else
#define	DWC2_EVCNT_ADD(a,b)	do { } while (/*CONSTCOND*/0)
#endif
#define	DWC2_EVCNT_INCR(a)	DWC2_EVCNT_ADD((a), 1)

#ifdef DWC2_DEBUG
#define	DPRINTFN(n,fmt,...) do {			\
	if (dwc2debug >= (n)) {			\
		printf("%s: " fmt,			\
		__FUNCTION__,## __VA_ARGS__);		\
	}						\
} while (0)
#define	DPRINTF(...)	DPRINTFN(1, __VA_ARGS__)
int dwc2debug = 0;
#else
#define	DPRINTF(...) do { } while (0)
#define	DPRINTFN(...) do { } while (0)
#endif

STATIC usbd_status	dwc2_open(struct usbd_pipe *);
STATIC int		dwc2_setaddr(struct usbd_device *, int);
STATIC void		dwc2_poll(struct usbd_bus *);
STATIC void		dwc2_softintr(void *);

STATIC struct usbd_xfer	*dwc2_allocx(struct usbd_bus *);
STATIC void		dwc2_freex(struct usbd_bus *, struct usbd_xfer *);

STATIC usbd_status	dwc2_root_ctrl_transfer(struct usbd_xfer *);
STATIC usbd_status	dwc2_root_ctrl_start(struct usbd_xfer *);
STATIC void		dwc2_root_ctrl_abort(struct usbd_xfer *);
STATIC void		dwc2_root_ctrl_close(struct usbd_pipe *);
STATIC void		dwc2_root_ctrl_done(struct usbd_xfer *);

STATIC usbd_status	dwc2_root_intr_transfer(struct usbd_xfer *);
STATIC usbd_status	dwc2_root_intr_start(struct usbd_xfer *);
STATIC void		dwc2_root_intr_abort(struct usbd_xfer *);
STATIC void		dwc2_root_intr_close(struct usbd_pipe *);
STATIC void		dwc2_root_intr_done(struct usbd_xfer *);

STATIC usbd_status	dwc2_device_ctrl_transfer(struct usbd_xfer *);
STATIC usbd_status	dwc2_device_ctrl_start(struct usbd_xfer *);
STATIC void		dwc2_device_ctrl_abort(struct usbd_xfer *);
STATIC void		dwc2_device_ctrl_close(struct usbd_pipe *);
STATIC void		dwc2_device_ctrl_done(struct usbd_xfer *);

STATIC usbd_status	dwc2_device_bulk_transfer(struct usbd_xfer *);
STATIC usbd_status	dwc2_device_bulk_start(struct usbd_xfer *);
STATIC void		dwc2_device_bulk_abort(struct usbd_xfer *);
STATIC void		dwc2_device_bulk_close(struct usbd_pipe *);
STATIC void		dwc2_device_bulk_done(struct usbd_xfer *);

STATIC usbd_status	dwc2_device_intr_transfer(struct usbd_xfer *);
STATIC usbd_status	dwc2_device_intr_start(struct usbd_xfer *);
STATIC void		dwc2_device_intr_abort(struct usbd_xfer *);
STATIC void		dwc2_device_intr_close(struct usbd_pipe *);
STATIC void		dwc2_device_intr_done(struct usbd_xfer *);

STATIC usbd_status	dwc2_device_isoc_transfer(struct usbd_xfer *);
STATIC usbd_status	dwc2_device_isoc_start(struct usbd_xfer *);
STATIC void		dwc2_device_isoc_abort(struct usbd_xfer *);
STATIC void		dwc2_device_isoc_close(struct usbd_pipe *);
STATIC void		dwc2_device_isoc_done(struct usbd_xfer *);

STATIC usbd_status	dwc2_device_start(struct usbd_xfer *);

STATIC void		dwc2_close_pipe(struct usbd_pipe *);
STATIC void		dwc2_abort_xfer(struct usbd_xfer *, usbd_status);

STATIC void		dwc2_device_clear_toggle(struct usbd_pipe *);
STATIC void		dwc2_noop(struct usbd_pipe *pipe);

STATIC int		dwc2_interrupt(struct dwc2_softc *);
STATIC void		dwc2_rhc(void *);

STATIC void		dwc2_timeout(void *);
STATIC void		dwc2_timeout_task(void *);

int			dwc2_check_core_version(struct dwc2_hsotg *);

#define DWC2_INTR_ENDPT 1

STATIC const struct usbd_bus_methods dwc2_bus_methods = {
	.open_pipe =	dwc2_open,
	.dev_setaddr =	dwc2_setaddr,
	.soft_intr =	dwc2_softintr,
	.do_poll =	dwc2_poll,
	.allocx =	dwc2_allocx,
	.freex =	dwc2_freex,
};

STATIC const struct usbd_pipe_methods dwc2_root_ctrl_methods = {
	.transfer =	dwc2_root_ctrl_transfer,
	.start =	dwc2_root_ctrl_start,
	.abort =	dwc2_root_ctrl_abort,
	.close =	dwc2_root_ctrl_close,
	.cleartoggle =	dwc2_noop,
	.done =		dwc2_root_ctrl_done,
};

STATIC const struct usbd_pipe_methods dwc2_root_intr_methods = {
	.transfer =	dwc2_root_intr_transfer,
	.start =	dwc2_root_intr_start,
	.abort =	dwc2_root_intr_abort,
	.close =	dwc2_root_intr_close,
	.cleartoggle =	dwc2_noop,
	.done =		dwc2_root_intr_done,
};

STATIC const struct usbd_pipe_methods dwc2_device_ctrl_methods = {
	.transfer =	dwc2_device_ctrl_transfer,
	.start =	dwc2_device_ctrl_start,
	.abort =	dwc2_device_ctrl_abort,
	.close =	dwc2_device_ctrl_close,
	.cleartoggle =	dwc2_noop,
	.done =		dwc2_device_ctrl_done,
};

STATIC const struct usbd_pipe_methods dwc2_device_intr_methods = {
	.transfer =	dwc2_device_intr_transfer,
	.start =	dwc2_device_intr_start,
	.abort =	dwc2_device_intr_abort,
	.close =	dwc2_device_intr_close,
	.cleartoggle =	dwc2_device_clear_toggle,
	.done =		dwc2_device_intr_done,
};

STATIC const struct usbd_pipe_methods dwc2_device_bulk_methods = {
	.transfer =	dwc2_device_bulk_transfer,
	.start =	dwc2_device_bulk_start,
	.abort =	dwc2_device_bulk_abort,
	.close =	dwc2_device_bulk_close,
	.cleartoggle =	dwc2_device_clear_toggle,
	.done =		dwc2_device_bulk_done,
};

STATIC const struct usbd_pipe_methods dwc2_device_isoc_methods = {
	.transfer =	dwc2_device_isoc_transfer,
	.start =	dwc2_device_isoc_start,
	.abort =	dwc2_device_isoc_abort,
	.close =	dwc2_device_isoc_close,
	.cleartoggle =	dwc2_noop,
	.done =		dwc2_device_isoc_done,
};

/*
 * Work around the half configured control (default) pipe when setting
 * the address of a device.
 */
STATIC int
dwc2_setaddr(struct usbd_device *dev, int addr)
{
	if (usbd_set_address(dev, addr))
		return (1);

	dev->address = addr;

	/*
	 * Re-establish the default pipe with the new address and the
	 * new max packet size.
	 */
	dwc2_close_pipe(dev->default_pipe);
	if (dwc2_open(dev->default_pipe))
		return (EINVAL);

	return (0);
}

struct usbd_xfer *
dwc2_allocx(struct usbd_bus *bus)
{
	struct dwc2_softc *sc = DWC2_BUS2SC(bus);
	struct dwc2_xfer *dxfer;

	DPRINTFN(10, "\n");

	DWC2_EVCNT_INCR(sc->sc_ev_xferpoolget);
	dxfer = pool_get(&sc->sc_xferpool, PR_NOWAIT | PR_ZERO);
	if (dxfer != NULL) {
#ifdef DIAGNOSTIC
		dxfer->xfer.busy_free = XFER_ONQU;
#endif
	}
	return (struct usbd_xfer *)dxfer;
}

void
dwc2_freex(struct usbd_bus *bus, struct usbd_xfer *xfer)
{
	struct dwc2_softc *sc = DWC2_BUS2SC(bus);

	DPRINTFN(10, "\n");

#ifdef DIAGNOSTIC
	if (xfer->busy_free != XFER_ONQU &&
	    xfer->status != USBD_NOT_STARTED) {
		DPRINTF("xfer=%p not busy, 0x%08x\n", xfer, xfer->busy_free);
	}
	xfer->busy_free = XFER_FREE;
#endif
	DWC2_EVCNT_INCR(sc->sc_ev_xferpoolput);
	pool_put(&sc->sc_xferpool, xfer);
}

STATIC void
dwc2_rhc(void *addr)
{
	struct dwc2_softc *sc = addr;
	struct usbd_xfer *xfer;
	u_char *p;

	DPRINTF("\n");
	mtx_enter(&sc->sc_lock);
	xfer = sc->sc_intrxfer;

	if (xfer == NULL) {
		/* Just ignore the change. */
		mtx_leave(&sc->sc_lock);
		return;

	}

	/* set port bit */
	p = KERNADDR(&xfer->dmabuf, 0);

	p[0] = 0x02;	/* we only have one port (1 << 1) */

	xfer->actlen = xfer->length;
	xfer->status = USBD_NORMAL_COMPLETION;

	usb_transfer_complete(xfer);
	mtx_leave(&sc->sc_lock);
}

STATIC void
dwc2_softintr(void *v)
{
	struct usbd_bus *bus = v;
	struct dwc2_softc *sc = DWC2_BUS2SC(bus);
	struct dwc2_hsotg *hsotg = sc->sc_hsotg;
	struct dwc2_xfer *dxfer, *next;
	TAILQ_HEAD(, dwc2_xfer) claimed = TAILQ_HEAD_INITIALIZER(claimed);

	/*
	 * Grab all the xfers that have not been aborted or timed out.
	 * Do so under a single lock -- without dropping it to run
	 * usb_transfer_complete as we go -- so that dwc2_abortx won't
	 * remove next out from under us during iteration when we've
	 * dropped the lock.
	 */
	mtx_enter(&hsotg->lock);
	TAILQ_FOREACH_SAFE(dxfer, &sc->sc_complete, xnext, next) {
		KASSERT(dxfer->xfer.status == USBD_IN_PROGRESS);
		KASSERT(dxfer->intr_status != USBD_CANCELLED);
		KASSERT(dxfer->intr_status != USBD_TIMEOUT);
		TAILQ_REMOVE(&sc->sc_complete, dxfer, xnext);
		TAILQ_INSERT_TAIL(&claimed, dxfer, xnext);
	}
	mtx_leave(&hsotg->lock);

	/* Now complete them.  */
	while (!TAILQ_EMPTY(&claimed)) {
		dxfer = TAILQ_FIRST(&claimed);
		KASSERT(dxfer->xfer.status == USBD_IN_PROGRESS);
		KASSERT(dxfer->intr_status != USBD_CANCELLED);
		KASSERT(dxfer->intr_status != USBD_TIMEOUT);
		TAILQ_REMOVE(&claimed, dxfer, xnext);

		dxfer->xfer.status = dxfer->intr_status;
		usb_transfer_complete(&dxfer->xfer);
	}
}

STATIC void
dwc2_timeout(void *addr)
{
	struct usbd_xfer *xfer = addr;
	struct dwc2_softc *sc = DWC2_XFER2SC(xfer);

	if (sc->sc_bus.dying) {
		dwc2_timeout_task(addr);
		return;
	}

	/* Execute the abort in a process context. */
	usb_init_task(&xfer->abort_task, dwc2_timeout_task, addr,
	    USB_TASK_TYPE_ABORT);
	usb_add_task(xfer->device, &xfer->abort_task);
}

STATIC void
dwc2_timeout_task(void *addr)
{
	struct usbd_xfer *xfer = addr;
	int s;

	s = splusb();
	dwc2_abort_xfer(xfer, USBD_TIMEOUT);
	splx(s);
}

usbd_status
dwc2_open(struct usbd_pipe *pipe)
{
	struct usbd_device *dev = pipe->device;
	struct dwc2_softc *sc = DWC2_PIPE2SC(pipe);
	struct dwc2_pipe *dpipe = DWC2_PIPE2DPIPE(pipe);
	usb_endpoint_descriptor_t *ed = pipe->endpoint->edesc;
	uint8_t addr = dev->address;
	uint8_t xfertype = UE_GET_XFERTYPE(ed->bmAttributes);
	usbd_status err;

	DPRINTF("pipe %p addr %d xfertype %d dir %s\n", pipe, addr, xfertype,
	    UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN ? "in" : "out");

	if (sc->sc_bus.dying) {
		return USBD_IOERROR;
	}

	if (addr == sc->sc_addr) {
		switch (ed->bEndpointAddress) {
		case USB_CONTROL_ENDPOINT:
			pipe->methods = &dwc2_root_ctrl_methods;
			break;
		case UE_DIR_IN | DWC2_INTR_ENDPT:
			pipe->methods = &dwc2_root_intr_methods;
			break;
		default:
			DPRINTF("bad bEndpointAddress 0x%02x\n",
			    ed->bEndpointAddress);
			return USBD_INVAL;
		}
		DPRINTF("root hub pipe open\n");
		return USBD_NORMAL_COMPLETION;
	}

	switch (xfertype) {
	case UE_CONTROL:
		pipe->methods = &dwc2_device_ctrl_methods;
		err = usb_allocmem(&sc->sc_bus, sizeof(usb_device_request_t),
		    0, USB_DMA_COHERENT, &dpipe->req_dma);
		if (err)
			return USBD_NOMEM;
		break;
	case UE_INTERRUPT:
		pipe->methods = &dwc2_device_intr_methods;
		break;
	case UE_ISOCHRONOUS:
		pipe->methods = &dwc2_device_isoc_methods;
		break;
	case UE_BULK:
		pipe->methods = &dwc2_device_bulk_methods;
		break;
	default:
		DPRINTF("bad xfer type %d\n", xfertype);
		return USBD_INVAL;
	}

	/* QH */
	dpipe->priv = NULL;

	return USBD_NORMAL_COMPLETION;
}

STATIC void
dwc2_poll(struct usbd_bus *bus)
{
	struct dwc2_softc *sc = DWC2_BUS2SC(bus);

	dwc2_interrupt(sc);
}

/*
 * Close a reqular pipe.
 * Assumes that there are no pending transactions.
 */
STATIC void
dwc2_close_pipe(struct usbd_pipe *pipe)
{
	/* nothing */
}

/*
 * Abort a device request.
 */
STATIC void
dwc2_abort_xfer(struct usbd_xfer *xfer, usbd_status status)
{
	struct dwc2_xfer *dxfer = DWC2_XFER2DXFER(xfer);
	struct dwc2_softc *sc = DWC2_XFER2SC(xfer);
	struct dwc2_hsotg *hsotg = sc->sc_hsotg;
	struct dwc2_xfer *d;
	int err;

	splsoftassert(IPL_SOFTUSB);

	DPRINTF("xfer %p pipe %p status 0x%08x\n", xfer, xfer->pipe,
	    xfer->status);

	/* XXX The stack should not call abort() in this case. */
	if (sc->sc_bus.dying || xfer->status == USBD_NOT_STARTED) {
		xfer->status = status;
		timeout_del(&xfer->timeout_handle);
		usb_rem_task(xfer->device, &xfer->abort_task);
		usb_transfer_complete(xfer);
		return;
	}

	KASSERT(xfer->status != USBD_CANCELLED);
	/* Transfer is already done. */
	if (xfer->status != USBD_IN_PROGRESS) {
		DPRINTF("%s: already done \n", __func__);
		return;
	}

	/* Prevent any timeout to kick in. */	
	timeout_del(&xfer->timeout_handle);
	usb_rem_task(xfer->device, &xfer->abort_task);

	/* Claim the transfer status as cancelled. */
	xfer->status = USBD_CANCELLED;

	KASSERTMSG((xfer->status == USBD_CANCELLED ||
		xfer->status == USBD_TIMEOUT),
	    "bad abort status: %d", xfer->status);

	mtx_enter(&hsotg->lock);

	/*
	 * Check whether we aborted or timed out after the hardware
	 * completion interrupt determined that it's done but before
	 * the soft interrupt could actually complete it.  If so, it's
	 * too late for the soft interrupt -- at this point we've
	 * already committed to abort it or time it out, so we need to
	 * take it off the softint's list of work in case the caller,
	 * say, frees the xfer before the softint runs.
	 *
	 * This logic is unusual among host controller drivers, and
	 * happens because dwc2 decides to complete xfers in the hard
	 * interrupt handler rather than in the soft interrupt handler,
	 * but usb_transfer_complete must be deferred to softint -- and
	 * we happened to swoop in between the hard interrupt and the
	 * soft interrupt.  Other host controller drivers do almost all
	 * processing in the softint so there's no intermediate stage.
	 *
	 * Fortunately, this linear search to discern the intermediate
	 * stage is not likely to be a serious performance impact
	 * because it happens only on abort or timeout.
	 */
	TAILQ_FOREACH(d, &sc->sc_complete, xnext) {
		if (d == dxfer) {
			TAILQ_REMOVE(&sc->sc_complete, dxfer, xnext);
			break;
		}
	}

	/*
	 * HC Step 1: Handle the hardware.
	 */
	err = dwc2_hcd_urb_dequeue(hsotg, dxfer->urb);
	if (err) {
		DPRINTF("dwc2_hcd_urb_dequeue failed\n");
	}

	mtx_leave(&hsotg->lock);

	/*
	 * Final Step: Notify completion to waiting xfers.
	 */
	usb_transfer_complete(xfer);
}

STATIC void
dwc2_noop(struct usbd_pipe *pipe)
{

}

STATIC void
dwc2_device_clear_toggle(struct usbd_pipe *pipe)
{
	DPRINTF("toggle %d -> 0", pipe->endpoint->savedtoggle);
}

/*
 * Data structures and routines to emulate the root hub.
 */

STATIC const usb_device_descriptor_t dwc2_devd = {
	.bLength = sizeof(usb_device_descriptor_t),
	.bDescriptorType = UDESC_DEVICE,
	.bcdUSB = {0x00, 0x02},
	.bDeviceClass = UDCLASS_HUB,
	.bDeviceSubClass = UDSUBCLASS_HUB,
	.bDeviceProtocol = UDPROTO_HSHUBSTT,
	.bMaxPacketSize = 64,
	.bcdDevice = {0x00, 0x01},
	.iManufacturer = 1,
	.iProduct = 2,
	.bNumConfigurations = 1,
};

struct dwc2_config_desc {
	usb_config_descriptor_t confd;
	usb_interface_descriptor_t ifcd;
	usb_endpoint_descriptor_t endpd;
} __packed;

STATIC const struct dwc2_config_desc dwc2_confd = {
	.confd = {
		.bLength = USB_CONFIG_DESCRIPTOR_SIZE,
		.bDescriptorType = UDESC_CONFIG,
		.wTotalLength[0] = sizeof(dwc2_confd),
		.bNumInterfaces = 1,
		.bConfigurationValue = 1,
		.iConfiguration = 0,
		.bmAttributes = UC_BUS_POWERED | UC_SELF_POWERED,
		.bMaxPower = 0,
	},
	.ifcd = {
		.bLength = USB_INTERFACE_DESCRIPTOR_SIZE,
		.bDescriptorType = UDESC_INTERFACE,
		.bInterfaceNumber = 0,
		.bAlternateSetting = 0,
		.bNumEndpoints = 1,
		.bInterfaceClass = UICLASS_HUB,
		.bInterfaceSubClass = UISUBCLASS_HUB,
		.bInterfaceProtocol = UIPROTO_HSHUBSTT,
		.iInterface = 0
	},
	.endpd = {
		.bLength = USB_ENDPOINT_DESCRIPTOR_SIZE,
		.bDescriptorType = UDESC_ENDPOINT,
		.bEndpointAddress = UE_DIR_IN | DWC2_INTR_ENDPT,
		.bmAttributes = UE_INTERRUPT,
		.wMaxPacketSize = {8, 0},			/* max packet */
		.bInterval = 255,
	},
};

STATIC usbd_status
dwc2_root_ctrl_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	err = usb_insert_transfer(xfer);
	if (err)
		return err;

	return dwc2_root_ctrl_start(SIMPLEQ_FIRST(&xfer->pipe->queue));
}

STATIC usbd_status
dwc2_root_ctrl_start(struct usbd_xfer *xfer)
{
	struct dwc2_softc *sc = DWC2_XFER2SC(xfer);
	usb_device_request_t *req;
	uint8_t *buf;
	uint16_t len;
	int value, index, l, s, totlen;
	usbd_status err = USBD_IOERROR;

	KASSERT(xfer->rqflags & URQ_REQUEST);

	if (sc->sc_bus.dying)
		return USBD_IOERROR;

	req = &xfer->request;

	DPRINTFN(4, "type=0x%02x request=%02x\n",
	    req->bmRequestType, req->bRequest);

	len = UGETW(req->wLength);
	value = UGETW(req->wValue);
	index = UGETW(req->wIndex);

	buf = len ? KERNADDR(&xfer->dmabuf, 0) : NULL;

	totlen = 0;

#define C(x,y) ((x) | ((y) << 8))
	switch (C(req->bRequest, req->bmRequestType)) {
	case C(UR_CLEAR_FEATURE, UT_WRITE_DEVICE):
	case C(UR_CLEAR_FEATURE, UT_WRITE_INTERFACE):
	case C(UR_CLEAR_FEATURE, UT_WRITE_ENDPOINT):
		/*
		 * DEVICE_REMOTE_WAKEUP and ENDPOINT_HALT are no-ops
		 * for the integrated root hub.
		 */
		break;
	case C(UR_GET_CONFIG, UT_READ_DEVICE):
		if (len > 0) {
			*buf = sc->sc_conf;
			totlen = 1;
		}
		break;
	case C(UR_GET_DESCRIPTOR, UT_READ_DEVICE):
		DPRINTFN(8, "wValue=0x%04x\n", value);

		if (len == 0)
			break;
		switch (value) {
		case C(0, UDESC_DEVICE):
			l = min(len, USB_DEVICE_DESCRIPTOR_SIZE);
			memcpy(buf, &dwc2_devd, l);
			buf += l;
			len -= l;
			totlen += l;

			break;
		case C(0, UDESC_CONFIG):
			l = min(len, sizeof(dwc2_confd));
			memcpy(buf, &dwc2_confd, l);
			buf += l;
			len -= l;
			totlen += l;

			break;
#define sd ((usb_string_descriptor_t *)buf)
		case C(0, UDESC_STRING):
			totlen = usbd_str(sd, len, "\001");
			break;
		case C(1, UDESC_STRING):
			totlen = usbd_str(sd, len, sc->sc_vendor);
			break;
		case C(2, UDESC_STRING):
			totlen = usbd_str(sd, len, "DWC2 root hub");
			break;
#undef sd
		default:
			goto fail;
		}
		break;
	case C(UR_GET_INTERFACE, UT_READ_INTERFACE):
		if (len > 0) {
			*buf = 0;
			totlen = 1;
		}
		break;
	case C(UR_GET_STATUS, UT_READ_DEVICE):
		if (len > 1) {
			USETW(((usb_status_t *)buf)->wStatus,UDS_SELF_POWERED);
			totlen = 2;
		}
		break;
	case C(UR_GET_STATUS, UT_READ_INTERFACE):
	case C(UR_GET_STATUS, UT_READ_ENDPOINT):
		if (len > 1) {
			USETW(((usb_status_t *)buf)->wStatus, 0);
			totlen = 2;
		}
		break;
	case C(UR_SET_ADDRESS, UT_WRITE_DEVICE):
		DPRINTF("UR_SET_ADDRESS, UT_WRITE_DEVICE: addr %d\n",
		    value);
		if (value >= USB_MAX_DEVICES)
			goto fail;

		sc->sc_addr = value;
		break;
	case C(UR_SET_CONFIG, UT_WRITE_DEVICE):
		if (value != 0 && value != 1)
			goto fail;

		sc->sc_conf = value;
		break;
	case C(UR_SET_DESCRIPTOR, UT_WRITE_DEVICE):
		break;
	case C(UR_SET_FEATURE, UT_WRITE_DEVICE):
	case C(UR_SET_FEATURE, UT_WRITE_INTERFACE):
	case C(UR_SET_FEATURE, UT_WRITE_ENDPOINT):
		err = USBD_IOERROR;
		goto fail;
	case C(UR_SET_INTERFACE, UT_WRITE_INTERFACE):
		break;
	case C(UR_SYNCH_FRAME, UT_WRITE_ENDPOINT):
		break;
	default:
		/* Hub requests - XXXNH len check? */
		err = dwc2_hcd_hub_control(sc->sc_hsotg,
		    C(req->bRequest, req->bmRequestType), value, index,
		    buf, len);
		if (err) {
			err = USBD_IOERROR;
			goto fail;
		}
		totlen = len;
	}
	xfer->actlen = totlen;
	err = USBD_NORMAL_COMPLETION;

fail:
	s = splusb();
	xfer->status = err;
	usb_transfer_complete(xfer);
	splx(s);

	return err;
}

STATIC void
dwc2_root_ctrl_abort(struct usbd_xfer *xfer)
{
}

STATIC void
dwc2_root_ctrl_close(struct usbd_pipe *pipe)
{
}

STATIC void
dwc2_root_ctrl_done(struct usbd_xfer *xfer)
{
}

STATIC usbd_status
dwc2_root_intr_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	err = usb_insert_transfer(xfer);
	if (err)
		return err;

	return dwc2_root_intr_start(SIMPLEQ_FIRST(&xfer->pipe->queue));
}

STATIC usbd_status
dwc2_root_intr_start(struct usbd_xfer *xfer)
{
	struct dwc2_softc *sc = DWC2_XFER2SC(xfer);

	if (sc->sc_bus.dying)
		return USBD_IOERROR;

	sc->sc_intrxfer = xfer;

	return USBD_IN_PROGRESS;
}

STATIC void
dwc2_root_intr_abort(struct usbd_xfer *xfer)
{
	struct dwc2_softc *sc = DWC2_XFER2SC(xfer);
	int s;

	sc->sc_intrxfer = NULL;

	xfer->status = USBD_CANCELLED;
	s = splusb();
	usb_transfer_complete(xfer);
	splx(s);
}

STATIC void
dwc2_root_intr_close(struct usbd_pipe *pipe)
{
}

STATIC void
dwc2_root_intr_done(struct usbd_xfer *xfer)
{
}

STATIC usbd_status
dwc2_device_ctrl_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	err = usb_insert_transfer(xfer);
	if (err)
		return err;

	return dwc2_device_ctrl_start(SIMPLEQ_FIRST(&xfer->pipe->queue));
}

STATIC usbd_status
dwc2_device_ctrl_start(struct usbd_xfer *xfer)
{
	struct dwc2_softc *sc = DWC2_XFER2SC(xfer);
	usbd_status err;

	KASSERT(xfer->rqflags & URQ_REQUEST);

	if (sc->sc_bus.dying)
		return USBD_IOERROR;

	err = dwc2_device_start(xfer);
	if (err)
		return err;

	return USBD_IN_PROGRESS;
}

STATIC void
dwc2_device_ctrl_abort(struct usbd_xfer *xfer)
{
	dwc2_abort_xfer(xfer, USBD_CANCELLED);
}

STATIC void
dwc2_device_ctrl_close(struct usbd_pipe *pipe)
{
	struct dwc2_softc * const sc = DWC2_PIPE2SC(pipe);
	struct dwc2_pipe * const dpipe = DWC2_PIPE2DPIPE(pipe);

	dwc2_close_pipe(pipe);
	usb_freemem(&sc->sc_bus, &dpipe->req_dma);
}

STATIC void
dwc2_device_ctrl_done(struct usbd_xfer *xfer)
{
	KASSERT(xfer->rqflags & URQ_REQUEST);
}

STATIC usbd_status
dwc2_device_bulk_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	err = usb_insert_transfer(xfer);
	if (err)
		return err;

	return dwc2_device_bulk_start(SIMPLEQ_FIRST(&xfer->pipe->queue));
}

STATIC usbd_status
dwc2_device_bulk_start(struct usbd_xfer *xfer)
{
	struct dwc2_softc *sc = DWC2_XFER2SC(xfer);
	usbd_status err;

	KASSERT(!(xfer->rqflags & URQ_REQUEST));

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	err = dwc2_device_start(xfer);
	if (err)
		return err;

	return USBD_IN_PROGRESS;
}

STATIC void
dwc2_device_bulk_abort(struct usbd_xfer *xfer)
{
	dwc2_abort_xfer(xfer, USBD_CANCELLED);
}

STATIC void
dwc2_device_bulk_close(struct usbd_pipe *pipe)
{
	dwc2_close_pipe(pipe);
}

STATIC void
dwc2_device_bulk_done(struct usbd_xfer *xfer)
{
}

STATIC usbd_status
dwc2_device_intr_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	err = usb_insert_transfer(xfer);
	if (err)
		return err;

	return dwc2_device_intr_start(SIMPLEQ_FIRST(&xfer->pipe->queue));
}

STATIC usbd_status
dwc2_device_intr_start(struct usbd_xfer *xfer)
{
	struct dwc2_softc *sc = DWC2_XFER2SC(xfer);
	usbd_status err;

        KASSERT(!(xfer->rqflags & URQ_REQUEST));

        if (sc->sc_bus.dying)
                return (USBD_IOERROR);

	err = dwc2_device_start(xfer);
	if (err)
		return err;

	return USBD_IN_PROGRESS;
}

STATIC void
dwc2_device_intr_abort(struct usbd_xfer *xfer)
{
	KASSERT(!xfer->pipe->repeat || xfer->pipe->intrxfer == xfer);

	dwc2_abort_xfer(xfer, USBD_CANCELLED);
}

STATIC void
dwc2_device_intr_close(struct usbd_pipe *pipe)
{
	dwc2_close_pipe(pipe);
}

STATIC void
dwc2_device_intr_done(struct usbd_xfer *xfer)
{
	if (xfer->pipe->repeat)
		dwc2_device_start(xfer);
}

usbd_status
dwc2_device_isoc_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	err = usb_insert_transfer(xfer);
	if (err)
		return err;

	return dwc2_device_isoc_start(SIMPLEQ_FIRST(&xfer->pipe->queue));
}

usbd_status
dwc2_device_isoc_start(struct usbd_xfer *xfer)
{
	struct dwc2_pipe *dpipe = DWC2_XFER2DPIPE(xfer);
	struct dwc2_softc *sc = DWC2_DPIPE2SC(dpipe);
	usbd_status err;

	/* Why would you do that anyway? */
	if (sc->sc_bus.use_polling)
		return (USBD_INVAL);

	err = dwc2_device_start(xfer);
	if (err)
		return err;

	return USBD_IN_PROGRESS;
}

void
dwc2_device_isoc_abort(struct usbd_xfer *xfer)
{
	dwc2_abort_xfer(xfer, USBD_CANCELLED);
}

void
dwc2_device_isoc_close(struct usbd_pipe *pipe)
{
	dwc2_close_pipe(pipe);
}

void
dwc2_device_isoc_done(struct usbd_xfer *xfer)
{
}

usbd_status
dwc2_device_start(struct usbd_xfer *xfer)
{
 	struct dwc2_xfer *dxfer = DWC2_XFER2DXFER(xfer);
	struct dwc2_pipe *dpipe = DWC2_XFER2DPIPE(xfer);
	struct dwc2_softc *sc = DWC2_XFER2SC(xfer);
	struct dwc2_hsotg *hsotg = sc->sc_hsotg;
	struct dwc2_hcd_urb *dwc2_urb;

	struct usbd_device *dev = xfer->pipe->device;
	usb_endpoint_descriptor_t *ed = xfer->pipe->endpoint->edesc;
	uint8_t addr = dev->address;
	uint8_t xfertype = UE_GET_XFERTYPE(ed->bmAttributes);
	uint8_t epnum = UE_GET_ADDR(ed->bEndpointAddress);
	uint8_t dir = UE_GET_DIR(ed->bEndpointAddress);
	uint32_t mps = UGETW(ed->wMaxPacketSize);
	uint32_t len;

	uint32_t flags = 0;
	uint32_t off = 0;
	int retval, err;
	int alloc_bandwidth = 0;

	DPRINTFN(1, "xfer=%p pipe=%p\n", xfer, xfer->pipe);

	if (xfertype == UE_ISOCHRONOUS ||
	    xfertype == UE_INTERRUPT) {
		mtx_enter(&hsotg->lock);
		if (!dwc2_hcd_is_bandwidth_allocated(hsotg, xfer))
			alloc_bandwidth = 1;
		mtx_leave(&hsotg->lock);
	}

	/*
	 * For Control pipe the direction is from the request, all other
	 * transfers have been set correctly at pipe open time.
	 */
	if (xfertype == UE_CONTROL) {
		usb_device_request_t *req = &xfer->request;

		DPRINTFN(3, "xfer=%p type=0x%02x request=0x%02x wValue=0x%04x "
		    "wIndex=0x%04x len=%d addr=%d endpt=%d dir=%s speed=%d "
		    "mps=%d\n",
		    xfer, req->bmRequestType, req->bRequest, UGETW(req->wValue),
		    UGETW(req->wIndex), UGETW(req->wLength), dev->address,
		    epnum, dir == UT_READ ? "in" :"out", dev->speed,
		    UE_GET_SIZE(mps));

		/* Copy request packet to our DMA buffer */
		memcpy(KERNADDR(&dpipe->req_dma, 0), req, sizeof(*req));
		usb_syncmem(&dpipe->req_dma, 0, sizeof(*req),
		    BUS_DMASYNC_PREWRITE);
		len = UGETW(req->wLength);
		if ((req->bmRequestType & UT_READ) == UT_READ) {
			dir = UE_DIR_IN;
		} else {
			dir = UE_DIR_OUT;
		}

		DPRINTFN(3, "req = %p dma = %llx len %d dir %s\n",
		    KERNADDR(&dpipe->req_dma, 0),
		    (long long)DMAADDR(&dpipe->req_dma, 0),
		    len, dir == UE_DIR_IN ? "in" : "out");
	} else if (xfertype == UE_ISOCHRONOUS) {
		DPRINTFN(3, "xfer=%p nframes=%d flags=%d addr=%d endpt=%d,"
		    " mps=%d dir %s\n", xfer, xfer->nframes, xfer->flags, addr,
		    epnum, UE_GET_SIZE(mps), dir == UT_READ ? "in" :"out");

#ifdef DIAGNOSTIC
		len = 0;
		for (size_t i = 0; i < xfer->nframes; i++)
			len += xfer->frlengths[i];
		if (len != xfer->length)
			panic("len (%d) != xfer->length (%d)", len,
			    xfer->length);
#endif
		len = xfer->length;
        } else {
                DPRINTFN(3, "xfer=%p len=%d flags=%d addr=%d endpt=%d,"
                    " mps=%d dir %s\n", xfer, xfer->length, xfer->flags, addr,
                    epnum, UE_GET_SIZE(mps), dir == UT_READ ? "in" :"out");

		len = xfer->length;
	}

	dxfer->urb = dwc2_hcd_urb_alloc(sc->sc_hsotg, xfer->nframes, M_NOWAIT);
	dwc2_urb = dxfer->urb;
	if (!dwc2_urb)
		return USBD_NOMEM;

	memset(dwc2_urb, 0, sizeof(*dwc2_urb) +
	    sizeof(dwc2_urb->iso_descs[0]) * xfer->nframes);

	dwc2_urb->priv = xfer;
	dwc2_urb->packet_count = xfer->nframes;

	dwc2_hcd_urb_set_pipeinfo(hsotg, dwc2_urb, addr, epnum, xfertype, dir,
	    UE_GET_SIZE(mps), UE_GET_TRANS(mps) + 1);

	if (xfertype == UE_CONTROL) {
		dwc2_urb->setup_usbdma = &dpipe->req_dma;
		dwc2_urb->setup_packet = KERNADDR(&dpipe->req_dma, 0);
		dwc2_urb->setup_dma = DMAADDR(&dpipe->req_dma, 0);
	} else {
		/* XXXNH - % mps required? */
		if ((xfer->flags & USBD_FORCE_SHORT_XFER) && (len %
		    UE_GET_SIZE(mps)) == 0)
		    flags |= URB_SEND_ZERO_PACKET;
	}
	flags |= URB_GIVEBACK_ASAP;

	/*
	 * control transfers with no data phase don't touch usbdma, but
	 * everything else does.
	 */
	if (!(xfertype == UE_CONTROL && len == 0)) {
		dwc2_urb->usbdma = &xfer->dmabuf;
		dwc2_urb->buf = KERNADDR(dwc2_urb->usbdma, 0);
		dwc2_urb->dma = DMAADDR(dwc2_urb->usbdma, 0);

		usb_syncmem(&xfer->dmabuf, 0, len,
		    dir == UE_DIR_IN ?
			BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);
 	}
	dwc2_urb->length = len;
 	dwc2_urb->flags = flags;
	dwc2_urb->status = -EINPROGRESS;

	if (xfertype == UE_INTERRUPT ||
	    xfertype == UE_ISOCHRONOUS) {
		uint16_t ival;

		if (xfertype == UE_INTERRUPT &&
		    dpipe->pipe.interval != USBD_DEFAULT_INTERVAL) {
			ival = dpipe->pipe.interval;
		} else {
			ival = ed->bInterval;
		}

		if (ival < 1) {
			retval = -ENODEV;
			goto fail;
		}
		if (dev->speed == USB_SPEED_HIGH ||
		   (dev->speed == USB_SPEED_FULL && xfertype == UE_ISOCHRONOUS)) {
			if (ival > 16) {
				/*
				 * illegal with HS/FS, but there were
				 * documentation bugs in the spec
				 */
				ival = 256;
			} else {
				ival = (1 << (ival - 1));
			}
		} else {
			if (xfertype == UE_INTERRUPT && ival < 10)
				ival = 10;
		}
		dwc2_urb->interval = ival;
	}

	xfer->actlen = 0;

	KASSERTMSG(xfer->nframes == 0 || xfertype == UE_ISOCHRONOUS,
	    "nframes %d xfertype %d\n", xfer->nframes, xfertype);

	off = 0;
	for (size_t i = 0; i < xfer->nframes; ++i) {
		DPRINTFN(3, "xfer=%p frame=%zu offset=%d length=%d\n", xfer, i,
		    off, xfer->frlengths[i]);

		dwc2_hcd_urb_set_iso_desc_params(dwc2_urb, i, off,
		    xfer->frlengths[i]);
		off += xfer->frlengths[i];
	}

	struct dwc2_qh *qh = dpipe->priv;
	struct dwc2_qtd *qtd;
	bool qh_allocated = false;

	/* Create QH for the endpoint if it doesn't exist */
	if (!qh) {
		qh = dwc2_hcd_qh_create(hsotg, dwc2_urb, M_ZERO | M_NOWAIT);
		if (!qh) {
			retval = -ENOMEM;
			goto fail;
		}
		dpipe->priv = qh;
		qh_allocated = true;
	}

	qtd = pool_get(&sc->sc_qtdpool, PR_NOWAIT | PR_ZERO);
	if (!qtd) {
		retval = -ENOMEM;
		goto fail1;
	}

	/* might need to check cpu_intr_p */
	mtx_enter(&hsotg->lock);
	retval = dwc2_hcd_urb_enqueue(hsotg, dwc2_urb, qh, qtd);
	if (retval)
		goto fail2;
	if (xfer->timeout && !sc->sc_bus.use_polling) {
		timeout_set(&xfer->timeout_handle, dwc2_timeout, xfer);
		timeout_add_msec(&xfer->timeout_handle, xfer->timeout);
	}
	xfer->status = USBD_IN_PROGRESS;

	if (alloc_bandwidth) {
		dwc2_allocate_bus_bandwidth(hsotg,
				dwc2_hcd_get_ep_bandwidth(hsotg, dpipe),
				xfer);
	}
	mtx_leave(&hsotg->lock);

	return USBD_IN_PROGRESS;

fail2:
	dwc2_urb->priv = NULL;
	mtx_leave(&hsotg->lock);
	pool_put(&sc->sc_qtdpool, qtd);

fail1:
	if (qh_allocated) {
		dpipe->priv = NULL;
		dwc2_hcd_qh_free(hsotg, qh);
	}
fail:

	switch (retval) {
	case -EINVAL:
	case -ENODEV:
		err = USBD_INVAL;
		break;
	case -ENOMEM:
		err = USBD_NOMEM;
		break;
	default:
		err = USBD_IOERROR;
	}

	return err;

}

int dwc2_intr(void *p)
{
	struct dwc2_softc *sc = p;
	struct dwc2_hsotg *hsotg;
	int ret = 0;

	if (sc == NULL)
		return 0;

	hsotg = sc->sc_hsotg;
//	mtx_enter(&hsotg->lock);

	if (sc->sc_bus.dying)
		goto done;

	if (sc->sc_bus.use_polling) {
		uint32_t intrs;

		intrs = dwc2_read_core_intr(hsotg);
		dwc2_writel(hsotg, intrs, GINTSTS);
	} else {
		ret = dwc2_interrupt(sc);
	}

done:
//	mtx_leave(&hsotg->lock);

	return ret;
}

int
dwc2_interrupt(struct dwc2_softc *sc)
{
	int ret = 0;

	if (sc->sc_hcdenabled)
		ret |= dwc2_handle_hcd_intr(sc->sc_hsotg);

	ret |= dwc2_handle_common_intr(sc->sc_hsotg);

	return ret;
}

int
dwc2_detach(struct dwc2_softc *sc, int flags)
{
	int rv = 0;

	if (sc->sc_child != NULL)
		rv = config_detach(sc->sc_child, flags);

	return rv;
}

int
dwc2_init(struct dwc2_softc *sc)
{
	int retval, err = 0;
	struct dwc2_hsotg *hsotg;

	sc->sc_bus.usbrev = USBREV_2_0;
	sc->sc_bus.methods = &dwc2_bus_methods;
	sc->sc_bus.pipe_size = sizeof(struct dwc2_pipe);
	sc->sc_hcdenabled = false;

	mtx_init(&sc->sc_lock, IPL_SOFTUSB);

	TAILQ_INIT(&sc->sc_complete);

	sc->sc_rhc_si = softintr_establish(IPL_SOFTUSB, dwc2_rhc, sc);

	pool_init(&sc->sc_xferpool, sizeof(struct dwc2_xfer), 0, IPL_VM, 0,
	    "dwc2xfer", NULL);
	pool_init(&sc->sc_qhpool, sizeof(struct dwc2_qh), 0, IPL_VM, 0,
	    "dwc2qh", NULL);
	pool_init(&sc->sc_qtdpool, sizeof(struct dwc2_qtd), 0, IPL_VM, 0,
	    "dwc2qtd", NULL);

	sc->sc_hsotg = malloc(sizeof(struct dwc2_hsotg), M_USBHC,
	    M_ZERO | M_WAITOK);
	sc->sc_hsotg->hsotg_sc = sc;
	sc->sc_hsotg->dev = &sc->sc_bus.bdev;
	sc->sc_hcdenabled = true;
	hsotg = sc->sc_hsotg;

	hsotg->dr_mode = USB_DR_MODE_HOST;

	/*
	 * Before performing any core related operations
	 * check core version.
 	 */
	retval = dwc2_check_core_version(hsotg);
	if (retval)
		goto fail2;

	/*
	 * Reset before dwc2_get_hwparams() then it could get power-on real
	 * reset value form registers.
	 */
	retval = dwc2_core_reset(hsotg, false);
	if (retval)
		goto fail2;

	/* Detect config values from hardware */
	retval = dwc2_get_hwparams(hsotg);
	if (retval)
		goto fail2;

	/*
	 * For OTG cores, set the force mode bits to reflect the value
	 * of dr_mode. Force mode bits should not be touched at any
	 * other time after this.
	 */
	dwc2_force_dr_mode(hsotg);

	retval = dwc2_init_params(hsotg);
	if (retval)
		goto fail2;
#if 0
	if (hsotg->dr_mode != USB_DR_MODE_HOST) {
		retval = dwc2_gadget_init(hsotg);
		if (retval)
			goto fail2;
		hsotg->gadget_enabled = 1;
	}
#endif
	if (hsotg->dr_mode != USB_DR_MODE_PERIPHERAL) {
		retval = dwc2_hcd_init(hsotg);
		if (retval) {
			if (hsotg->gadget_enabled)
				dwc2_hsotg_remove(hsotg);
			goto fail2;
		}
	    hsotg->hcd_enabled = 1;
        }

	hsotg->hibernated = 0;

	return 0;

fail2:
	err = -retval;
	free(sc->sc_hsotg, M_USBHC, sizeof(struct dwc2_hsotg));
	softintr_disestablish(sc->sc_rhc_si);

	return err;
}

void
dw_timeout(void *arg)
{
	struct delayed_work *dw = arg;

	task_set(&dw->work, dw->dw_fn, dw->dw_arg);
	task_add(dw->dw_wq, &dw->work);
}

/*** platform.c ***************************************************************/

int dwc2_check_core_version(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;

	/*
	 * Attempt to ensure this device is really a DWC_otg Controller.
	 * Read and verify the GSNPSID register contents. The value should be
	 * 0x45f4xxxx, 0x5531xxxx or 0x5532xxxx
	 */

	hw->snpsid = dwc2_readl(hsotg, GSNPSID);
	if ((hw->snpsid & GSNPSID_ID_MASK) != DWC2_OTG_ID &&
	    (hw->snpsid & GSNPSID_ID_MASK) != DWC2_FS_IOT_ID &&
	    (hw->snpsid & GSNPSID_ID_MASK) != DWC2_HS_IOT_ID) {
		dev_err(hsotg->dev, "Bad value for GSNPSID: 0x%08x\n",
			hw->snpsid);
		return -ENODEV;
	}

	dev_dbg(hsotg->dev, "Core Release: %1x.%1x%1x%1x (snpsid=%x)\n",
		hw->snpsid >> 12 & 0xf, hw->snpsid >> 8 & 0xf,
		hw->snpsid >> 4 & 0xf, hw->snpsid & 0xf, hw->snpsid);
	return 0;
}
