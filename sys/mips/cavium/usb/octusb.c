#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

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
 * This file contains the driver for Octeon Executive Library USB
 * Controller driver API.
 */

/* TODO: The root HUB port callback is not yet implemented. */

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

#define	USB_DEBUG_VAR octusbdebug

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_transfer.h>
#include <dev/usb/usb_device.h>
#include <dev/usb/usb_hub.h>
#include <dev/usb/usb_util.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <contrib/octeon-sdk/cvmx-usb.h>

#include <mips/cavium/usb/octusb.h>

#define	OCTUSB_BUS2SC(bus) \
   ((struct octusb_softc *)(((uint8_t *)(bus)) - \
    ((uint8_t *)&(((struct octusb_softc *)0)->sc_bus))))

#ifdef USB_DEBUG
static int octusbdebug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, octusb, CTLFLAG_RW, 0, "OCTUSB");
SYSCTL_INT(_hw_usb_octusb, OID_AUTO, debug, CTLFLAG_RWTUN,
    &octusbdebug, 0, "OCTUSB debug level");
#endif

struct octusb_std_temp {
	octusb_cmd_t *func;
	struct octusb_td *td;
	struct octusb_td *td_next;
	struct usb_page_cache *pc;
	uint32_t offset;
	uint32_t len;
	uint8_t	short_pkt;
	uint8_t	setup_alt_next;
};

extern struct usb_bus_methods octusb_bus_methods;
extern struct usb_pipe_methods octusb_device_bulk_methods;
extern struct usb_pipe_methods octusb_device_ctrl_methods;
extern struct usb_pipe_methods octusb_device_intr_methods;
extern struct usb_pipe_methods octusb_device_isoc_methods;

static void octusb_standard_done(struct usb_xfer *);
static void octusb_device_done(struct usb_xfer *, usb_error_t);
static void octusb_timeout(void *);
static void octusb_do_poll(struct usb_bus *);

static cvmx_usb_speed_t
octusb_convert_speed(enum usb_dev_speed speed)
{
	;				/* indent fix */
	switch (speed) {
	case USB_SPEED_HIGH:
		return (CVMX_USB_SPEED_HIGH);
	case USB_SPEED_FULL:
		return (CVMX_USB_SPEED_FULL);
	default:
		return (CVMX_USB_SPEED_LOW);
	}
}

static cvmx_usb_transfer_t
octusb_convert_ep_type(uint8_t ep_type)
{
	;				/* indent fix */
	switch (ep_type & UE_XFERTYPE) {
	case UE_CONTROL:
		return (CVMX_USB_TRANSFER_CONTROL);
	case UE_INTERRUPT:
		return (CVMX_USB_TRANSFER_INTERRUPT);
	case UE_ISOCHRONOUS:
		return (CVMX_USB_TRANSFER_ISOCHRONOUS);
	case UE_BULK:
		return (CVMX_USB_TRANSFER_BULK);
	default:
		return (0);		/* should not happen */
	}
}

static uint8_t
octusb_host_alloc_endpoint(struct octusb_td *td)
{
	struct octusb_softc *sc;
	int ep_handle;

	if (td->qh->fixup_pending)
		return (1);		/* busy */

	if (td->qh->ep_allocated)
		return (0);		/* success */

	/* get softc */
	sc = td->qh->sc;

	ep_handle = cvmx_usb_open_pipe(
	    &sc->sc_port[td->qh->root_port_index].state,
	    0,
	    td->qh->dev_addr,
	    td->qh->ep_num & UE_ADDR,
	    octusb_convert_speed(td->qh->dev_speed),
	    td->qh->max_packet_size,
	    octusb_convert_ep_type(td->qh->ep_type),
	    (td->qh->ep_num & UE_DIR_IN) ? CVMX_USB_DIRECTION_IN :
	    CVMX_USB_DIRECTION_OUT,
	    td->qh->ep_interval,
	    (td->qh->dev_speed == USB_SPEED_HIGH) ? td->qh->ep_mult : 0,
	    td->qh->hs_hub_addr,
	    td->qh->hs_hub_port);

	if (ep_handle < 0) {
		DPRINTFN(1, "cvmx_usb_open_pipe failed: %d\n", ep_handle);
		return (1);		/* busy */
	}

	cvmx_usb_set_toggle(
	    &sc->sc_port[td->qh->root_port_index].state,
	    ep_handle, td->qh->ep_toggle_next);

	td->qh->fixup_handle = -1;
	td->qh->fixup_complete = 0;
	td->qh->fixup_len = 0;
	td->qh->fixup_off = 0;
	td->qh->fixup_pending = 0;
	td->qh->fixup_actlen = 0;

	td->qh->ep_handle = ep_handle;
	td->qh->ep_allocated = 1;

	return (0);			/* success */
}

static void
octusb_host_free_endpoint(struct octusb_td *td)
{
	struct octusb_softc *sc;

	if (td->qh->ep_allocated == 0)
		return;

	/* get softc */
	sc = td->qh->sc;

	if (td->qh->fixup_handle >= 0) {
		/* cancel, if any */
		cvmx_usb_cancel(&sc->sc_port[td->qh->root_port_index].state,
		    td->qh->ep_handle, td->qh->fixup_handle);
	}
	cvmx_usb_close_pipe(&sc->sc_port[td->qh->root_port_index].state, td->qh->ep_handle);

	td->qh->ep_allocated = 0;
}

static void
octusb_complete_cb(cvmx_usb_state_t *state,
    cvmx_usb_callback_t reason,
    cvmx_usb_complete_t status,
    int pipe_handle, int submit_handle,
    int bytes_transferred, void *user_data)
{
	struct octusb_td *td;

	if (reason != CVMX_USB_CALLBACK_TRANSFER_COMPLETE)
		return;

	td = user_data;

	td->qh->fixup_complete = 1;
	td->qh->fixup_pending = 0;
	td->qh->fixup_actlen = bytes_transferred;
	td->qh->fixup_handle = -1;

	switch (status) {
	case CVMX_USB_COMPLETE_SUCCESS:
	case CVMX_USB_COMPLETE_SHORT:
		td->error_any = 0;
		td->error_stall = 0;
		break;
	case CVMX_USB_COMPLETE_STALL:
		td->error_stall = 1;
		td->error_any = 1;
		break;
	default:
		td->error_any = 1;
		break;
	}
}

static uint8_t
octusb_host_control_header_tx(struct octusb_td *td)
{
	int status;

	/* allocate endpoint and check pending */
	if (octusb_host_alloc_endpoint(td))
		return (1);		/* busy */

	/* check error */
	if (td->error_any)
		return (0);		/* done */

	if (td->qh->fixup_complete != 0) {
		/* clear complete flag */
		td->qh->fixup_complete = 0;

		/* flush data */
		usb_pc_cpu_invalidate(td->qh->fixup_pc);
		return (0);		/* done */
	}
	/* verify length */
	if (td->remainder != 8) {
		td->error_any = 1;
		return (0);		/* done */
	}
	usbd_copy_out(td->pc, td->offset, td->qh->fixup_buf, 8);

	/* update offset and remainder */
	td->offset += 8;
	td->remainder -= 8;

	/* setup data length and offset */
	td->qh->fixup_len = UGETW(td->qh->fixup_buf + 6);
	td->qh->fixup_off = 0;

	if (td->qh->fixup_len > (OCTUSB_MAX_FIXUP - 8)) {
		td->error_any = 1;
		return (0);		/* done */
	}
	/* do control IN request */
	if (td->qh->fixup_buf[0] & UE_DIR_IN) {

		struct octusb_softc *sc;

		/* get softc */
		sc = td->qh->sc;

		/* flush data */
		usb_pc_cpu_flush(td->qh->fixup_pc);

		status = cvmx_usb_submit_control(
		    &sc->sc_port[td->qh->root_port_index].state,
		    td->qh->ep_handle, td->qh->fixup_phys,
		    td->qh->fixup_phys + 8, td->qh->fixup_len,
		    &octusb_complete_cb, td);
		/* check status */
		if (status < 0) {
			td->error_any = 1;
			return (0);	/* done */
		}
		td->qh->fixup_handle = status;
		td->qh->fixup_pending = 1;
		td->qh->fixup_complete = 0;

		return (1);		/* busy */
	}
	return (0);			/* done */
}

static uint8_t
octusb_host_control_data_tx(struct octusb_td *td)
{
	uint32_t rem;

	/* allocate endpoint and check pending */
	if (octusb_host_alloc_endpoint(td))
		return (1);		/* busy */

	/* check error */
	if (td->error_any)
		return (0);		/* done */

	rem = td->qh->fixup_len - td->qh->fixup_off;

	if (td->remainder > rem) {
		td->error_any = 1;
		DPRINTFN(1, "Excess setup transmit data\n");
		return (0);		/* done */
	}
	usbd_copy_out(td->pc, td->offset, td->qh->fixup_buf +
	    td->qh->fixup_off + 8, td->remainder);

	td->offset += td->remainder;
	td->qh->fixup_off += td->remainder;
	td->remainder = 0;

	return (0);			/* done */
}

static uint8_t
octusb_host_control_data_rx(struct octusb_td *td)
{
	uint32_t rem;

	/* allocate endpoint and check pending */
	if (octusb_host_alloc_endpoint(td))
		return (1);		/* busy */

	/* check error */
	if (td->error_any)
		return (0);		/* done */

	/* copy data from buffer */
	rem = td->qh->fixup_actlen - td->qh->fixup_off;

	if (rem > td->remainder)
		rem = td->remainder;

	usbd_copy_in(td->pc, td->offset, td->qh->fixup_buf +
	    td->qh->fixup_off + 8, rem);

	td->offset += rem;
	td->remainder -= rem;
	td->qh->fixup_off += rem;

	return (0);			/* done */
}

static uint8_t
octusb_host_control_status_tx(struct octusb_td *td)
{
	int status;

	/* allocate endpoint and check pending */
	if (octusb_host_alloc_endpoint(td))
		return (1);		/* busy */

	/* check error */
	if (td->error_any)
		return (0);		/* done */

	if (td->qh->fixup_complete != 0) {
		/* clear complete flag */
		td->qh->fixup_complete = 0;
		/* done */
		return (0);
	}
	/* do control IN request */
	if (!(td->qh->fixup_buf[0] & UE_DIR_IN)) {

		struct octusb_softc *sc;

		/* get softc */
		sc = td->qh->sc;

		/* flush data */
		usb_pc_cpu_flush(td->qh->fixup_pc);

		/* start USB transfer */
		status = cvmx_usb_submit_control(
		    &sc->sc_port[td->qh->root_port_index].state,
		    td->qh->ep_handle, td->qh->fixup_phys,
		    td->qh->fixup_phys + 8, td->qh->fixup_len,
		    &octusb_complete_cb, td);

		/* check status */
		if (status < 0) {
			td->error_any = 1;
			return (0);	/* done */
		}
		td->qh->fixup_handle = status;
		td->qh->fixup_pending = 1;
		td->qh->fixup_complete = 0;

		return (1);		/* busy */
	}
	return (0);			/* done */
}

static uint8_t
octusb_non_control_data_tx(struct octusb_td *td)
{
	struct octusb_softc *sc;
	uint32_t rem;
	int status;

	/* allocate endpoint and check pending */
	if (octusb_host_alloc_endpoint(td))
		return (1);		/* busy */

	/* check error */
	if (td->error_any)
		return (0);		/* done */

	if ((td->qh->fixup_complete != 0) &&
	    ((td->qh->ep_type & UE_XFERTYPE) == UE_ISOCHRONOUS)) {
		td->qh->fixup_complete = 0;
		return (0);		/* done */
	}
	/* check complete */
	if (td->remainder == 0) {
		if (td->short_pkt)
			return (0);	/* complete */
		/* else need to send a zero length packet */
		rem = 0;
		td->short_pkt = 1;
	} else {
		/* get maximum length */
		rem = OCTUSB_MAX_FIXUP % td->qh->max_frame_size;
		rem = OCTUSB_MAX_FIXUP - rem;

		if (rem == 0) {
			/* should not happen */
			DPRINTFN(1, "Fixup buffer is too small\n");
			td->error_any = 1;
			return (0);	/* done */
		}
		/* get minimum length */
		if (rem > td->remainder) {
			rem = td->remainder;
			if ((rem == 0) || (rem % td->qh->max_frame_size))
				td->short_pkt = 1;
		}
		/* copy data into fixup buffer */
		usbd_copy_out(td->pc, td->offset, td->qh->fixup_buf, rem);

		/* flush data */
		usb_pc_cpu_flush(td->qh->fixup_pc);

		/* pre-increment TX buffer offset */
		td->offset += rem;
		td->remainder -= rem;
	}

	/* get softc */
	sc = td->qh->sc;

	switch (td->qh->ep_type & UE_XFERTYPE) {
	case UE_ISOCHRONOUS:
		td->qh->iso_pkt.offset = 0;
		td->qh->iso_pkt.length = rem;
		td->qh->iso_pkt.status = 0;
		/* start USB transfer */
		status = cvmx_usb_submit_isochronous(&sc->sc_port[td->qh->root_port_index].state,
		    td->qh->ep_handle, 1, CVMX_USB_ISOCHRONOUS_FLAGS_ALLOW_SHORT |
		    CVMX_USB_ISOCHRONOUS_FLAGS_ASAP, 1, &td->qh->iso_pkt,
		    td->qh->fixup_phys, rem, &octusb_complete_cb, td);
		break;
	case UE_BULK:
		/* start USB transfer */
		status = cvmx_usb_submit_bulk(&sc->sc_port[td->qh->root_port_index].state,
		    td->qh->ep_handle, td->qh->fixup_phys, rem, &octusb_complete_cb, td);
		break;
	case UE_INTERRUPT:
		/* start USB transfer (interrupt or interrupt) */
		status = cvmx_usb_submit_interrupt(&sc->sc_port[td->qh->root_port_index].state,
		    td->qh->ep_handle, td->qh->fixup_phys, rem, &octusb_complete_cb, td);
		break;
	default:
		status = -1;
		break;
	}

	/* check status */
	if (status < 0) {
		td->error_any = 1;
		return (0);		/* done */
	}
	td->qh->fixup_handle = status;
	td->qh->fixup_len = rem;
	td->qh->fixup_pending = 1;
	td->qh->fixup_complete = 0;

	return (1);			/* busy */
}

static uint8_t
octusb_non_control_data_rx(struct octusb_td *td)
{
	struct octusb_softc *sc;
	uint32_t rem;
	int status;
	uint8_t got_short;

	/* allocate endpoint and check pending */
	if (octusb_host_alloc_endpoint(td))
		return (1);		/* busy */

	/* check error */
	if (td->error_any)
		return (0);		/* done */

	got_short = 0;

	if (td->qh->fixup_complete != 0) {

		/* invalidate data */
		usb_pc_cpu_invalidate(td->qh->fixup_pc);

		rem = td->qh->fixup_actlen;

		/* verify transfer length */
		if (rem != td->qh->fixup_len) {
			if (rem < td->qh->fixup_len) {
				/* we have a short packet */
				td->short_pkt = 1;
				got_short = 1;
			} else {
				/* invalid USB packet */
				td->error_any = 1;
				return (0);	/* we are complete */
			}
		}
		/* copy data into fixup buffer */
		usbd_copy_in(td->pc, td->offset, td->qh->fixup_buf, rem);

		/* post-increment RX buffer offset */
		td->offset += rem;
		td->remainder -= rem;

		td->qh->fixup_complete = 0;

		if ((td->qh->ep_type & UE_XFERTYPE) == UE_ISOCHRONOUS)
			return (0);	/* done */
	}
	/* check if we are complete */
	if ((td->remainder == 0) || got_short) {
		if (td->short_pkt) {
			/* we are complete */
			return (0);
		}
		/* else need to receive a zero length packet */
		rem = 0;
		td->short_pkt = 1;
	} else {
		/* get maximum length */
		rem = OCTUSB_MAX_FIXUP % td->qh->max_frame_size;
		rem = OCTUSB_MAX_FIXUP - rem;

		if (rem == 0) {
			/* should not happen */
			DPRINTFN(1, "Fixup buffer is too small\n");
			td->error_any = 1;
			return (0);	/* done */
		}
		/* get minimum length */
		if (rem > td->remainder)
			rem = td->remainder;
	}

	/* invalidate data */
	usb_pc_cpu_invalidate(td->qh->fixup_pc);

	/* get softc */
	sc = td->qh->sc;

	switch (td->qh->ep_type & UE_XFERTYPE) {
	case UE_ISOCHRONOUS:
		td->qh->iso_pkt.offset = 0;
		td->qh->iso_pkt.length = rem;
		td->qh->iso_pkt.status = 0;
		/* start USB transfer */
		status = cvmx_usb_submit_isochronous(&sc->sc_port[td->qh->root_port_index].state,
		    td->qh->ep_handle, 1, CVMX_USB_ISOCHRONOUS_FLAGS_ALLOW_SHORT |
		    CVMX_USB_ISOCHRONOUS_FLAGS_ASAP, 1, &td->qh->iso_pkt,
		    td->qh->fixup_phys, rem, &octusb_complete_cb, td);
		break;
	case UE_BULK:
		/* start USB transfer */
		status = cvmx_usb_submit_bulk(&sc->sc_port[td->qh->root_port_index].state,
		    td->qh->ep_handle, td->qh->fixup_phys, rem, &octusb_complete_cb, td);
		break;
	case UE_INTERRUPT:
		/* start USB transfer */
		status = cvmx_usb_submit_interrupt(&sc->sc_port[td->qh->root_port_index].state,
		    td->qh->ep_handle, td->qh->fixup_phys, rem, &octusb_complete_cb, td);
		break;
	default:
		status = -1;
		break;
	}

	/* check status */
	if (status < 0) {
		td->error_any = 1;
		return (0);		/* done */
	}
	td->qh->fixup_handle = status;
	td->qh->fixup_len = rem;
	td->qh->fixup_pending = 1;
	td->qh->fixup_complete = 0;

	return (1);			/* busy */
}

static uint8_t
octusb_xfer_do_fifo(struct usb_xfer *xfer)
{
	struct octusb_td *td;

	DPRINTFN(8, "\n");

	td = xfer->td_transfer_cache;

	while (1) {
		if ((td->func) (td)) {
			/* operation in progress */
			break;
		}
		if (((void *)td) == xfer->td_transfer_last) {
			goto done;
		}
		if (td->error_any) {
			goto done;
		} else if (td->remainder > 0) {
			/*
			 * We had a short transfer. If there is no
			 * alternate next, stop processing !
			 */
			if (td->alt_next == 0)
				goto done;
		}
		/*
		 * Fetch the next transfer descriptor and transfer
		 * some flags to the next transfer descriptor
		 */
		td = td->obj_next;
		xfer->td_transfer_cache = td;
	}
	return (1);			/* not complete */

done:
	/* compute all actual lengths */

	octusb_standard_done(xfer);

	return (0);			/* complete */
}

static usb_error_t
octusb_standard_done_sub(struct usb_xfer *xfer)
{
	struct octusb_td *td;
	uint32_t len;
	usb_error_t error;

	DPRINTFN(8, "\n");

	td = xfer->td_transfer_cache;

	do {
		len = td->remainder;

		if (xfer->aframes != xfer->nframes) {
			/*
		         * Verify the length and subtract
		         * the remainder from "frlengths[]":
		         */
			if (len > xfer->frlengths[xfer->aframes]) {
				td->error_any = 1;
			} else {
				xfer->frlengths[xfer->aframes] -= len;
			}
		}
		/* Check for transfer error */
		if (td->error_any) {
			/* the transfer is finished */
			error = td->error_stall ? USB_ERR_STALLED : USB_ERR_IOERROR;
			td = NULL;
			break;
		}
		/* Check for short transfer */
		if (len > 0) {
			if (xfer->flags_int.short_frames_ok) {
				/* follow alt next */
				if (td->alt_next) {
					td = td->obj_next;
				} else {
					td = NULL;
				}
			} else {
				/* the transfer is finished */
				td = NULL;
			}
			error = 0;
			break;
		}
		td = td->obj_next;

		/* this USB frame is complete */
		error = 0;
		break;

	} while (0);

	/* update transfer cache */

	xfer->td_transfer_cache = td;

	return (error);
}

static void
octusb_standard_done(struct usb_xfer *xfer)
{
	struct octusb_softc *sc;
	struct octusb_qh *qh;
	usb_error_t error = 0;

	DPRINTFN(12, "xfer=%p endpoint=%p transfer done\n",
	    xfer, xfer->endpoint);

	/* reset scanner */

	xfer->td_transfer_cache = xfer->td_transfer_first;

	if (xfer->flags_int.control_xfr) {

		if (xfer->flags_int.control_hdr)
			error = octusb_standard_done_sub(xfer);

		xfer->aframes = 1;

		if (xfer->td_transfer_cache == NULL)
			goto done;
	}
	while (xfer->aframes != xfer->nframes) {

		error = octusb_standard_done_sub(xfer);

		xfer->aframes++;

		if (xfer->td_transfer_cache == NULL)
			goto done;
	}

	if (xfer->flags_int.control_xfr &&
	    !xfer->flags_int.control_act)
		error = octusb_standard_done_sub(xfer);

done:
	/* update data toggle */

	qh = xfer->qh_start[0];
	sc = qh->sc;

	xfer->endpoint->toggle_next =
	    cvmx_usb_get_toggle(
	    &sc->sc_port[qh->root_port_index].state,
	    qh->ep_handle) ? 1 : 0;

	octusb_device_done(xfer, error);
}

static void
octusb_interrupt_poll(struct octusb_softc *sc)
{
	struct usb_xfer *xfer;
	uint8_t x;

	/* poll all ports */
	for (x = 0; x != sc->sc_noport; x++)
		cvmx_usb_poll(&sc->sc_port[x].state);

repeat:
	TAILQ_FOREACH(xfer, &sc->sc_bus.intr_q.head, wait_entry) {
		if (!octusb_xfer_do_fifo(xfer)) {
			/* queue has been modified */
			goto repeat;
		}
	}
}

static void
octusb_start_standard_chain(struct usb_xfer *xfer)
{
	DPRINTFN(8, "\n");

	/* poll one time */
	if (octusb_xfer_do_fifo(xfer)) {

		/* put transfer on interrupt queue */
		usbd_transfer_enqueue(&xfer->xroot->bus->intr_q, xfer);

		/* start timeout, if any */
		if (xfer->timeout != 0) {
			usbd_transfer_timeout_ms(xfer,
			    &octusb_timeout, xfer->timeout);
		}
	}
}

void
octusb_iterate_hw_softc(struct usb_bus *bus, usb_bus_mem_sub_cb_t *cb)
{

}

usb_error_t
octusb_init(struct octusb_softc *sc)
{
	cvmx_usb_initialize_flags_t flags;
	int status;
	uint8_t x;

	/* flush all cache into memory */

	usb_bus_mem_flush_all(&sc->sc_bus, &octusb_iterate_hw_softc);

	/* set up the bus struct */
	sc->sc_bus.methods = &octusb_bus_methods;

	/* get number of ports */
	sc->sc_noport = cvmx_usb_get_num_ports();

	/* check number of ports */
	if (sc->sc_noport > OCTUSB_MAX_PORTS)
		sc->sc_noport = OCTUSB_MAX_PORTS;

	/* set USB revision */
	sc->sc_bus.usbrev = USB_REV_2_0;

	/* flags for port initialization */
	flags = CVMX_USB_INITIALIZE_FLAGS_CLOCK_AUTO;
#ifdef USB_DEBUG
	if (octusbdebug > 100)
		flags |= CVMX_USB_INITIALIZE_FLAGS_DEBUG_ALL;
#endif

	USB_BUS_LOCK(&sc->sc_bus);

	/* setup all ports */
	for (x = 0; x != sc->sc_noport; x++) {
		status = cvmx_usb_initialize(&sc->sc_port[x].state, x, flags);
		if (status < 0)
			sc->sc_port[x].disabled = 1;
	}

	USB_BUS_UNLOCK(&sc->sc_bus);

	/* catch lost interrupts */
	octusb_do_poll(&sc->sc_bus);

	return (0);
}

usb_error_t
octusb_uninit(struct octusb_softc *sc)
{
	uint8_t x;

	USB_BUS_LOCK(&sc->sc_bus);

	for (x = 0; x != sc->sc_noport; x++) {
		if (sc->sc_port[x].disabled == 0)
			cvmx_usb_shutdown(&sc->sc_port[x].state);
	}
	USB_BUS_UNLOCK(&sc->sc_bus);

	return (0);

}

static void
octusb_suspend(struct octusb_softc *sc)
{
	/* TODO */
}

static void
octusb_resume(struct octusb_softc *sc)
{
	/* TODO */
}

/*------------------------------------------------------------------------*
 *	octusb_interrupt - OCTUSB interrupt handler
 *------------------------------------------------------------------------*/
void
octusb_interrupt(struct octusb_softc *sc)
{
	USB_BUS_LOCK(&sc->sc_bus);

	DPRINTFN(16, "real interrupt\n");

	/* poll all the USB transfers */
	octusb_interrupt_poll(sc);

	USB_BUS_UNLOCK(&sc->sc_bus);
}

/*------------------------------------------------------------------------*
 *	octusb_timeout - OCTUSB transfer timeout handler
 *------------------------------------------------------------------------*/
static void
octusb_timeout(void *arg)
{
	struct usb_xfer *xfer = arg;

	DPRINTF("xfer=%p\n", xfer);

	USB_BUS_LOCK_ASSERT(xfer->xroot->bus, MA_OWNED);

	/* transfer is transferred */
	octusb_device_done(xfer, USB_ERR_TIMEOUT);
}

/*------------------------------------------------------------------------*
 *	octusb_do_poll - OCTUSB poll transfers
 *------------------------------------------------------------------------*/
static void
octusb_do_poll(struct usb_bus *bus)
{
	struct octusb_softc *sc = OCTUSB_BUS2SC(bus);

	USB_BUS_LOCK(&sc->sc_bus);
	octusb_interrupt_poll(sc);
	USB_BUS_UNLOCK(&sc->sc_bus);
}

static void
octusb_setup_standard_chain_sub(struct octusb_std_temp *temp)
{
	struct octusb_td *td;

	/* get current Transfer Descriptor */
	td = temp->td_next;
	temp->td = td;

	/* prepare for next TD */
	temp->td_next = td->obj_next;

	/* fill out the Transfer Descriptor */
	td->func = temp->func;
	td->pc = temp->pc;
	td->offset = temp->offset;
	td->remainder = temp->len;
	td->error_any = 0;
	td->error_stall = 0;
	td->short_pkt = temp->short_pkt;
	td->alt_next = temp->setup_alt_next;
}

static void
octusb_setup_standard_chain(struct usb_xfer *xfer)
{
	struct octusb_std_temp temp;
	struct octusb_td *td;
	uint32_t x;

	DPRINTFN(9, "addr=%d endpt=%d sumlen=%d speed=%d\n",
	    xfer->address, UE_GET_ADDR(xfer->endpointno),
	    xfer->sumlen, usbd_get_speed(xfer->xroot->udev));

	/* setup starting point */
	td = xfer->td_start[0];
	xfer->td_transfer_first = td;
	xfer->td_transfer_cache = td;

	temp.td = NULL;
	temp.td_next = td;
	temp.setup_alt_next = xfer->flags_int.short_frames_ok;
	temp.offset = 0;

	/* check if we should prepend a setup message */

	if (xfer->flags_int.control_xfr) {

		if (xfer->flags_int.control_hdr) {

			temp.func = &octusb_host_control_header_tx;
			temp.len = xfer->frlengths[0];
			temp.pc = xfer->frbuffers + 0;
			temp.short_pkt = temp.len ? 1 : 0;

			/* check for last frame */
			if (xfer->nframes == 1) {
				/*
				 * no STATUS stage yet, SETUP is
				 * last
				 */
				if (xfer->flags_int.control_act)
					temp.setup_alt_next = 0;
			}
			octusb_setup_standard_chain_sub(&temp);
		}
		x = 1;
	} else {
		x = 0;
	}

	if (x != xfer->nframes) {
		if (xfer->endpointno & UE_DIR_IN) {
			if (xfer->flags_int.control_xfr)
				temp.func = &octusb_host_control_data_rx;
			else
				temp.func = &octusb_non_control_data_rx;
		} else {
			if (xfer->flags_int.control_xfr)
				temp.func = &octusb_host_control_data_tx;
			else
				temp.func = &octusb_non_control_data_tx;
		}

		/* setup "pc" pointer */
		temp.pc = xfer->frbuffers + x;
	}
	while (x != xfer->nframes) {

		/* DATA0 or DATA1 message */

		temp.len = xfer->frlengths[x];

		x++;

		if (x == xfer->nframes) {
			if (xfer->flags_int.control_xfr) {
				/* no STATUS stage yet, DATA is last */
				if (xfer->flags_int.control_act)
					temp.setup_alt_next = 0;
			} else {
				temp.setup_alt_next = 0;
			}
		}
		if (temp.len == 0) {

			/* make sure that we send an USB packet */

			temp.short_pkt = 0;

		} else {

			/* regular data transfer */

			temp.short_pkt = (xfer->flags.force_short_xfer) ? 0 : 1;
		}

		octusb_setup_standard_chain_sub(&temp);

		if (xfer->flags_int.isochronous_xfr) {
			/* get next data offset */
			temp.offset += temp.len;
		} else {
			/* get next Page Cache pointer */
			temp.pc = xfer->frbuffers + x;
		}
	}

	/* check if we should append a status stage */

	if (xfer->flags_int.control_xfr &&
	    !xfer->flags_int.control_act) {

		temp.func = &octusb_host_control_status_tx;
		temp.len = 0;
		temp.pc = NULL;
		temp.short_pkt = 0;
		temp.setup_alt_next = 0;

		octusb_setup_standard_chain_sub(&temp);
	}
	/* must have at least one frame! */
	td = temp.td;
	xfer->td_transfer_last = td;

	/* properly setup QH */

	td->qh->ep_allocated = 0;
	td->qh->ep_toggle_next = xfer->endpoint->toggle_next ? 1 : 0;
}

/*------------------------------------------------------------------------*
 *	octusb_device_done - OCTUSB transfers done code
 *
 * NOTE: This function can be called more than one time in a row.
 *------------------------------------------------------------------------*/
static void
octusb_device_done(struct usb_xfer *xfer, usb_error_t error)
{
	USB_BUS_LOCK_ASSERT(xfer->xroot->bus, MA_OWNED);

	DPRINTFN(2, "xfer=%p, endpoint=%p, error=%d\n",
	    xfer, xfer->endpoint, error);

	/*
	 * 1) Free any endpoints.
	 * 2) Control transfers can be split and we should not re-open
	 *    the data pipe between transactions unless there is an error.
	 */
	if ((xfer->flags_int.control_act == 0) || (error != 0)) {
		struct octusb_td *td;

		td = xfer->td_start[0];

		octusb_host_free_endpoint(td);
	}
	/* dequeue transfer and start next transfer */
	usbd_transfer_done(xfer, error);
}

/*------------------------------------------------------------------------*
 * octusb bulk support
 *------------------------------------------------------------------------*/
static void
octusb_device_bulk_open(struct usb_xfer *xfer)
{
	return;
}

static void
octusb_device_bulk_close(struct usb_xfer *xfer)
{
	octusb_device_done(xfer, USB_ERR_CANCELLED);
}

static void
octusb_device_bulk_enter(struct usb_xfer *xfer)
{
	return;
}

static void
octusb_device_bulk_start(struct usb_xfer *xfer)
{
	/* setup TDs */
	octusb_setup_standard_chain(xfer);
	octusb_start_standard_chain(xfer);
}

struct usb_pipe_methods octusb_device_bulk_methods =
{
	.open = octusb_device_bulk_open,
	.close = octusb_device_bulk_close,
	.enter = octusb_device_bulk_enter,
	.start = octusb_device_bulk_start,
};

/*------------------------------------------------------------------------*
 * octusb control support
 *------------------------------------------------------------------------*/
static void
octusb_device_ctrl_open(struct usb_xfer *xfer)
{
	return;
}

static void
octusb_device_ctrl_close(struct usb_xfer *xfer)
{
	octusb_device_done(xfer, USB_ERR_CANCELLED);
}

static void
octusb_device_ctrl_enter(struct usb_xfer *xfer)
{
	return;
}

static void
octusb_device_ctrl_start(struct usb_xfer *xfer)
{
	/* setup TDs */
	octusb_setup_standard_chain(xfer);
	octusb_start_standard_chain(xfer);
}

struct usb_pipe_methods octusb_device_ctrl_methods =
{
	.open = octusb_device_ctrl_open,
	.close = octusb_device_ctrl_close,
	.enter = octusb_device_ctrl_enter,
	.start = octusb_device_ctrl_start,
};

/*------------------------------------------------------------------------*
 * octusb interrupt support
 *------------------------------------------------------------------------*/
static void
octusb_device_intr_open(struct usb_xfer *xfer)
{
	return;
}

static void
octusb_device_intr_close(struct usb_xfer *xfer)
{
	octusb_device_done(xfer, USB_ERR_CANCELLED);
}

static void
octusb_device_intr_enter(struct usb_xfer *xfer)
{
	return;
}

static void
octusb_device_intr_start(struct usb_xfer *xfer)
{
	/* setup TDs */
	octusb_setup_standard_chain(xfer);
	octusb_start_standard_chain(xfer);
}

struct usb_pipe_methods octusb_device_intr_methods =
{
	.open = octusb_device_intr_open,
	.close = octusb_device_intr_close,
	.enter = octusb_device_intr_enter,
	.start = octusb_device_intr_start,
};

/*------------------------------------------------------------------------*
 * octusb isochronous support
 *------------------------------------------------------------------------*/
static void
octusb_device_isoc_open(struct usb_xfer *xfer)
{
	return;
}

static void
octusb_device_isoc_close(struct usb_xfer *xfer)
{
	octusb_device_done(xfer, USB_ERR_CANCELLED);
}

static void
octusb_device_isoc_enter(struct usb_xfer *xfer)
{
	struct octusb_softc *sc = OCTUSB_BUS2SC(xfer->xroot->bus);
	uint32_t temp;
	uint32_t frame_count;
	uint32_t fs_frames;

	DPRINTFN(5, "xfer=%p next=%d nframes=%d\n",
	    xfer, xfer->endpoint->isoc_next, xfer->nframes);

	/* get the current frame index */

	frame_count = cvmx_usb_get_frame_number(
	    &sc->sc_port[xfer->xroot->udev->port_index].state);

	/*
	 * check if the frame index is within the window where the frames
	 * will be inserted
	 */
	temp = (frame_count - xfer->endpoint->isoc_next) & 0x7FF;

	if (usbd_get_speed(xfer->xroot->udev) == USB_SPEED_HIGH) {
		fs_frames = (xfer->nframes + 7) / 8;
	} else {
		fs_frames = xfer->nframes;
	}

	if ((xfer->endpoint->is_synced == 0) || (temp < fs_frames)) {
		/*
		 * If there is data underflow or the pipe queue is
		 * empty we schedule the transfer a few frames ahead
		 * of the current frame position. Else two isochronous
		 * transfers might overlap.
		 */
		xfer->endpoint->isoc_next = (frame_count + 3) & 0x7FF;
		xfer->endpoint->is_synced = 1;
		DPRINTFN(2, "start next=%d\n", xfer->endpoint->isoc_next);
	}
	/*
	 * compute how many milliseconds the insertion is ahead of the
	 * current frame position:
	 */
	temp = (xfer->endpoint->isoc_next - frame_count) & 0x7FF;

	/*
	 * pre-compute when the isochronous transfer will be finished:
	 */
	xfer->isoc_time_complete =
	    usb_isoc_time_expand(&sc->sc_bus, frame_count) + temp +
	    fs_frames;

	/* compute frame number for next insertion */
	xfer->endpoint->isoc_next += fs_frames;
}

static void
octusb_device_isoc_start(struct usb_xfer *xfer)
{
	/* setup TDs */
	octusb_setup_standard_chain(xfer);
	octusb_start_standard_chain(xfer);
}

struct usb_pipe_methods octusb_device_isoc_methods =
{
	.open = octusb_device_isoc_open,
	.close = octusb_device_isoc_close,
	.enter = octusb_device_isoc_enter,
	.start = octusb_device_isoc_start,
};

/*------------------------------------------------------------------------*
 * OCTUSB root HUB support
 *------------------------------------------------------------------------*
 * Simulate a hardware HUB by handling all the necessary requests.
 *------------------------------------------------------------------------*/
static const
struct usb_device_descriptor octusb_devd = {
	.bLength = sizeof(octusb_devd),
	.bDescriptorType = UDESC_DEVICE,
	.bcdUSB = {0x00, 0x02},
	.bDeviceClass = UDCLASS_HUB,
	.bDeviceSubClass = UDSUBCLASS_HUB,
	.bDeviceProtocol = UDPROTO_FSHUB,
	.bMaxPacketSize = 64,
	.idVendor = {0},
	.idProduct = {0},
	.bcdDevice = {0x00, 0x01},
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 0,
	.bNumConfigurations = 1,
};

static const
struct usb_device_qualifier octusb_odevd = {
	.bLength = sizeof(octusb_odevd),
	.bDescriptorType = UDESC_DEVICE_QUALIFIER,
	.bcdUSB = {0x00, 0x02},
	.bDeviceClass = UDCLASS_HUB,
	.bDeviceSubClass = UDSUBCLASS_HUB,
	.bDeviceProtocol = UDPROTO_FSHUB,
	.bMaxPacketSize0 = 0,
	.bNumConfigurations = 0,
	.bReserved = 0,
};

static const
struct octusb_config_desc octusb_confd = {
	.confd = {
		.bLength = sizeof(struct usb_config_descriptor),
		.bDescriptorType = UDESC_CONFIG,
		.wTotalLength[0] = sizeof(octusb_confd),
		.bNumInterface = 1,
		.bConfigurationValue = 1,
		.iConfiguration = 0,
		.bmAttributes = UC_SELF_POWERED,
		.bMaxPower = 0		/* max power */
	},
	.ifcd = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = UDESC_INTERFACE,
		.bNumEndpoints = 1,
		.bInterfaceClass = UICLASS_HUB,
		.bInterfaceSubClass = UISUBCLASS_HUB,
		.bInterfaceProtocol = UIPROTO_FSHUB,
	},
	.endpd = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = UDESC_ENDPOINT,
		.bEndpointAddress = UE_DIR_IN | OCTUSB_INTR_ENDPT,
		.bmAttributes = UE_INTERRUPT,
		.wMaxPacketSize[0] = 8,	/* max packet (63 ports) */
		.bInterval = 255,
	},
};

static const
struct usb_hub_descriptor_min octusb_hubd =
{
	.bDescLength = sizeof(octusb_hubd),
	.bDescriptorType = UDESC_HUB,
	.bNbrPorts = 2,
	.wHubCharacteristics = {UHD_PWR_NO_SWITCH | UHD_OC_INDIVIDUAL, 0},
	.bPwrOn2PwrGood = 50,
	.bHubContrCurrent = 0,
	.DeviceRemovable = {0x00},	/* all ports are removable */
};

static usb_error_t
octusb_roothub_exec(struct usb_device *udev,
    struct usb_device_request *req, const void **pptr, uint16_t *plength)
{
	struct octusb_softc *sc = OCTUSB_BUS2SC(udev->bus);
	const void *ptr;
	const char *str_ptr;
	uint16_t value;
	uint16_t index;
	uint16_t status;
	uint16_t change;
	uint16_t len;
	usb_error_t err;
	cvmx_usb_port_status_t usb_port_status;

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	/* XXX disable power save mode, hence it is not supported */
	udev->power_mode = USB_POWER_MODE_ON;

	/* buffer reset */
	ptr = (const void *)&sc->sc_hub_desc.temp;
	len = 0;
	err = 0;

	value = UGETW(req->wValue);
	index = UGETW(req->wIndex);

	DPRINTFN(3, "type=0x%02x request=0x%02x wLen=0x%04x "
	    "wValue=0x%04x wIndex=0x%04x\n",
	    req->bmRequestType, req->bRequest,
	    UGETW(req->wLength), value, index);

#define	C(x,y) ((x) | ((y) << 8))
	switch (C(req->bRequest, req->bmRequestType)) {
	case C(UR_CLEAR_FEATURE, UT_WRITE_DEVICE):
	case C(UR_CLEAR_FEATURE, UT_WRITE_INTERFACE):
	case C(UR_CLEAR_FEATURE, UT_WRITE_ENDPOINT):
		break;
	case C(UR_GET_CONFIG, UT_READ_DEVICE):
		len = 1;
		sc->sc_hub_desc.temp[0] = sc->sc_conf;
		break;
	case C(UR_GET_DESCRIPTOR, UT_READ_DEVICE):
		switch (value >> 8) {
		case UDESC_DEVICE:
			if ((value & 0xff) != 0) {
				err = USB_ERR_IOERROR;
				goto done;
			}
			len = sizeof(octusb_devd);

			ptr = (const void *)&octusb_devd;
			break;

		case UDESC_DEVICE_QUALIFIER:
			if ((value & 0xff) != 0) {
				err = USB_ERR_IOERROR;
				goto done;
			}
			len = sizeof(octusb_odevd);
			ptr = (const void *)&octusb_odevd;
			break;

		case UDESC_CONFIG:
			if ((value & 0xff) != 0) {
				err = USB_ERR_IOERROR;
				goto done;
			}
			len = sizeof(octusb_confd);
			ptr = (const void *)&octusb_confd;
			break;

		case UDESC_STRING:
			switch (value & 0xff) {
			case 0:	/* Language table */
				str_ptr = "\001";
				break;

			case 1:	/* Vendor */
				str_ptr = "Cavium Networks";
				break;

			case 2:	/* Product */
				str_ptr = "OCTUSB Root HUB";
				break;

			default:
				str_ptr = "";
				break;
			}

			len = usb_make_str_desc(sc->sc_hub_desc.temp,
			    sizeof(sc->sc_hub_desc.temp), str_ptr);
			break;

		default:
			err = USB_ERR_IOERROR;
			goto done;
		}
		break;
	case C(UR_GET_INTERFACE, UT_READ_INTERFACE):
		len = 1;
		sc->sc_hub_desc.temp[0] = 0;
		break;
	case C(UR_GET_STATUS, UT_READ_DEVICE):
		len = 2;
		USETW(sc->sc_hub_desc.stat.wStatus, UDS_SELF_POWERED);
		break;
	case C(UR_GET_STATUS, UT_READ_INTERFACE):
	case C(UR_GET_STATUS, UT_READ_ENDPOINT):
		len = 2;
		USETW(sc->sc_hub_desc.stat.wStatus, 0);
		break;
	case C(UR_SET_ADDRESS, UT_WRITE_DEVICE):
		if (value >= OCTUSB_MAX_DEVICES) {
			err = USB_ERR_IOERROR;
			goto done;
		}
		sc->sc_addr = value;
		break;
	case C(UR_SET_CONFIG, UT_WRITE_DEVICE):
		if ((value != 0) && (value != 1)) {
			err = USB_ERR_IOERROR;
			goto done;
		}
		sc->sc_conf = value;
		break;
	case C(UR_SET_DESCRIPTOR, UT_WRITE_DEVICE):
		break;
	case C(UR_SET_FEATURE, UT_WRITE_DEVICE):
	case C(UR_SET_FEATURE, UT_WRITE_INTERFACE):
	case C(UR_SET_FEATURE, UT_WRITE_ENDPOINT):
		err = USB_ERR_IOERROR;
		goto done;
	case C(UR_SET_INTERFACE, UT_WRITE_INTERFACE):
		break;
	case C(UR_SYNCH_FRAME, UT_WRITE_ENDPOINT):
		break;
		/* Hub requests */
	case C(UR_CLEAR_FEATURE, UT_WRITE_CLASS_DEVICE):
		break;
	case C(UR_CLEAR_FEATURE, UT_WRITE_CLASS_OTHER):
		DPRINTFN(4, "UR_CLEAR_PORT_FEATURE "
		    "port=%d feature=%d\n",
		    index, value);
		if ((index < 1) ||
		    (index > sc->sc_noport) ||
		    sc->sc_port[index - 1].disabled) {
			err = USB_ERR_IOERROR;
			goto done;
		}
		index--;

		switch (value) {
		case UHF_PORT_ENABLE:
			cvmx_usb_disable(&sc->sc_port[index].state);
			break;
		case UHF_PORT_SUSPEND:
		case UHF_PORT_RESET:
			break;
		case UHF_C_PORT_CONNECTION:
			cvmx_usb_set_status(&sc->sc_port[index].state,
			    cvmx_usb_get_status(&sc->sc_port[index].state));
			break;
		case UHF_C_PORT_ENABLE:
			cvmx_usb_set_status(&sc->sc_port[index].state,
			    cvmx_usb_get_status(&sc->sc_port[index].state));
			break;
		case UHF_C_PORT_OVER_CURRENT:
			cvmx_usb_set_status(&sc->sc_port[index].state,
			    cvmx_usb_get_status(&sc->sc_port[index].state));
			break;
		case UHF_C_PORT_RESET:
			sc->sc_isreset = 0;
			goto done;
		case UHF_C_PORT_SUSPEND:
			break;
		case UHF_PORT_CONNECTION:
		case UHF_PORT_OVER_CURRENT:
		case UHF_PORT_POWER:
		case UHF_PORT_LOW_SPEED:
		default:
			err = USB_ERR_IOERROR;
			goto done;
		}
		break;
	case C(UR_GET_DESCRIPTOR, UT_READ_CLASS_DEVICE):
		if ((value & 0xff) != 0) {
			err = USB_ERR_IOERROR;
			goto done;
		}
		sc->sc_hubd = octusb_hubd;
		sc->sc_hubd.bNbrPorts = sc->sc_noport;
		len = sizeof(sc->sc_hubd);
		ptr = (const void *)&sc->sc_hubd;
		break;
	case C(UR_GET_STATUS, UT_READ_CLASS_DEVICE):
		len = 16;
		memset(sc->sc_hub_desc.temp, 0, 16);
		break;
	case C(UR_GET_STATUS, UT_READ_CLASS_OTHER):
		if ((index < 1) ||
		    (index > sc->sc_noport) ||
		    sc->sc_port[index - 1].disabled) {
			err = USB_ERR_IOERROR;
			goto done;
		}
		index--;

		usb_port_status = cvmx_usb_get_status(&sc->sc_port[index].state);

		status = change = 0;
		if (usb_port_status.connected)
			status |= UPS_CURRENT_CONNECT_STATUS;
		if (usb_port_status.port_enabled)
			status |= UPS_PORT_ENABLED;
		if (usb_port_status.port_over_current)
			status |= UPS_OVERCURRENT_INDICATOR;
		if (usb_port_status.port_powered)
			status |= UPS_PORT_POWER;

		switch (usb_port_status.port_speed) {
		case CVMX_USB_SPEED_HIGH:
			status |= UPS_HIGH_SPEED;
			break;
		case CVMX_USB_SPEED_FULL:
			break;
		default:
			status |= UPS_LOW_SPEED;
			break;
		}

		if (usb_port_status.connect_change)
			change |= UPS_C_CONNECT_STATUS;
		if (sc->sc_isreset)
			change |= UPS_C_PORT_RESET;

		USETW(sc->sc_hub_desc.ps.wPortStatus, status);
		USETW(sc->sc_hub_desc.ps.wPortChange, change);

		len = sizeof(sc->sc_hub_desc.ps);
		break;
	case C(UR_SET_DESCRIPTOR, UT_WRITE_CLASS_DEVICE):
		err = USB_ERR_IOERROR;
		goto done;
	case C(UR_SET_FEATURE, UT_WRITE_CLASS_DEVICE):
		break;
	case C(UR_SET_FEATURE, UT_WRITE_CLASS_OTHER):
		if ((index < 1) ||
		    (index > sc->sc_noport) ||
		    sc->sc_port[index - 1].disabled) {
			err = USB_ERR_IOERROR;
			goto done;
		}
		index--;

		switch (value) {
		case UHF_PORT_ENABLE:
			break;
		case UHF_PORT_RESET:
			cvmx_usb_disable(&sc->sc_port[index].state);
			if (cvmx_usb_enable(&sc->sc_port[index].state)) {
				err = USB_ERR_IOERROR;
				goto done;
			}
			sc->sc_isreset = 1;
			goto done;
		case UHF_PORT_POWER:
			/* pretend we turned on power */
			goto done;
		case UHF_PORT_SUSPEND:
		case UHF_C_PORT_CONNECTION:
		case UHF_C_PORT_ENABLE:
		case UHF_C_PORT_OVER_CURRENT:
		case UHF_PORT_CONNECTION:
		case UHF_PORT_OVER_CURRENT:
		case UHF_PORT_LOW_SPEED:
		case UHF_C_PORT_SUSPEND:
		case UHF_C_PORT_RESET:
		default:
			err = USB_ERR_IOERROR;
			goto done;
		}
		break;
	default:
		err = USB_ERR_IOERROR;
		goto done;
	}
done:
	*plength = len;
	*pptr = ptr;
	return (err);
}

static void
octusb_xfer_setup(struct usb_setup_params *parm)
{
	struct usb_page_search page_info;
	struct usb_page_cache *pc;
	struct octusb_softc *sc;
	struct octusb_qh *qh;
	struct usb_xfer *xfer;
	struct usb_device *hub;
	void *last_obj;
	uint32_t n;
	uint32_t ntd;

	sc = OCTUSB_BUS2SC(parm->udev->bus);
	xfer = parm->curr_xfer;
	qh = NULL;

	/*
	 * NOTE: This driver does not use any of the parameters that
	 * are computed from the following values. Just set some
	 * reasonable dummies:
	 */

	parm->hc_max_packet_size = 0x400;
	parm->hc_max_packet_count = 3;
	parm->hc_max_frame_size = 0xC00;

	usbd_transfer_setup_sub(parm);

	if (parm->err)
		return;

	/* Allocate a queue head */

	if (usbd_transfer_setup_sub_malloc(
	    parm, &pc, sizeof(struct octusb_qh),
	    USB_HOST_ALIGN, 1)) {
		parm->err = USB_ERR_NOMEM;
		return;
	}
	if (parm->buf) {
		usbd_get_page(pc, 0, &page_info);

		qh = page_info.buffer;

		/* fill out QH */

		qh->sc = OCTUSB_BUS2SC(xfer->xroot->bus);
		qh->max_frame_size = xfer->max_frame_size;
		qh->max_packet_size = xfer->max_packet_size;
		qh->ep_num = xfer->endpointno;
		qh->ep_type = xfer->endpoint->edesc->bmAttributes;
		qh->dev_addr = xfer->address;
		qh->dev_speed = usbd_get_speed(xfer->xroot->udev);
		qh->root_port_index = xfer->xroot->udev->port_index;
		/* We need Octeon USB HUB's port index, not the local port */
		hub = xfer->xroot->udev->parent_hub;
		while(hub && hub->parent_hub) {
			qh->root_port_index = hub->port_index;
			hub = hub->parent_hub;
		}

		switch (xfer->endpoint->edesc->bmAttributes & UE_XFERTYPE) {
		case UE_INTERRUPT:
			if (usbd_get_speed(xfer->xroot->udev) == USB_SPEED_HIGH)
				qh->ep_interval = xfer->interval * 8;
			else
				qh->ep_interval = xfer->interval * 1;
			break;
		case UE_ISOCHRONOUS:
			qh->ep_interval = 1 << xfer->fps_shift;
			break;
		default:
			qh->ep_interval = 0;
			break;
		}

		qh->ep_mult = xfer->max_packet_count & 3;
		qh->hs_hub_addr = xfer->xroot->udev->hs_hub_addr;
		qh->hs_hub_port = xfer->xroot->udev->hs_port_no;
	}
	xfer->qh_start[0] = qh;

	/* Allocate a fixup buffer */

	if (usbd_transfer_setup_sub_malloc(
	    parm, &pc, OCTUSB_MAX_FIXUP,
	    OCTUSB_MAX_FIXUP, 1)) {
		parm->err = USB_ERR_NOMEM;
		return;
	}
	if (parm->buf) {
		usbd_get_page(pc, 0, &page_info);

		qh->fixup_phys = page_info.physaddr;
		qh->fixup_pc = pc;
		qh->fixup_buf = page_info.buffer;
	}
	/* Allocate transfer descriptors */

	last_obj = NULL;

	ntd = xfer->nframes + 1 /* STATUS */ + 1 /* SYNC */ ;

	if (usbd_transfer_setup_sub_malloc(
	    parm, &pc, sizeof(struct octusb_td),
	    USB_HOST_ALIGN, ntd)) {
		parm->err = USB_ERR_NOMEM;
		return;
	}
	if (parm->buf) {
		for (n = 0; n != ntd; n++) {
			struct octusb_td *td;

			usbd_get_page(pc + n, 0, &page_info);

			td = page_info.buffer;

			td->qh = qh;
			td->obj_next = last_obj;

			last_obj = td;
		}
	}
	xfer->td_start[0] = last_obj;
}

static void
octusb_ep_init(struct usb_device *udev, struct usb_endpoint_descriptor *edesc,
    struct usb_endpoint *ep)
{
	struct octusb_softc *sc = OCTUSB_BUS2SC(udev->bus);

	DPRINTFN(2, "endpoint=%p, addr=%d, endpt=%d, mode=%d (%d)\n",
	    ep, udev->address, edesc->bEndpointAddress,
	    udev->flags.usb_mode, sc->sc_addr);

	if (udev->device_index != sc->sc_addr) {
		switch (edesc->bmAttributes & UE_XFERTYPE) {
		case UE_CONTROL:
			ep->methods = &octusb_device_ctrl_methods;
			break;
		case UE_INTERRUPT:
			ep->methods = &octusb_device_intr_methods;
			break;
		case UE_ISOCHRONOUS:
			if (udev->speed != USB_SPEED_LOW)
				ep->methods = &octusb_device_isoc_methods;
			break;
		case UE_BULK:
			ep->methods = &octusb_device_bulk_methods;
			break;
		default:
			/* do nothing */
			break;
		}
	}
}

static void
octusb_xfer_unsetup(struct usb_xfer *xfer)
{
	DPRINTF("Nothing to do.\n");
}

static void
octusb_get_dma_delay(struct usb_device *udev, uint32_t *pus)
{
	/* DMA delay - wait until any use of memory is finished */
	*pus = (2125);			/* microseconds */
}

static void
octusb_device_resume(struct usb_device *udev)
{
	DPRINTF("Nothing to do.\n");
}

static void
octusb_device_suspend(struct usb_device *udev)
{
	DPRINTF("Nothing to do.\n");
}

static void
octusb_set_hw_power(struct usb_bus *bus)
{
	DPRINTF("Nothing to do.\n");
}

static void
octusb_set_hw_power_sleep(struct usb_bus *bus, uint32_t state)
{
	struct octusb_softc *sc = OCTUSB_BUS2SC(bus);

	switch (state) {
	case USB_HW_POWER_SUSPEND:
		octusb_suspend(sc);
		break;
	case USB_HW_POWER_SHUTDOWN:
		octusb_uninit(sc);
		break;
	case USB_HW_POWER_RESUME:
		octusb_resume(sc);
		break;
	default:
		break;
	}
}

struct usb_bus_methods octusb_bus_methods = {
	.endpoint_init = octusb_ep_init,
	.xfer_setup = octusb_xfer_setup,
	.xfer_unsetup = octusb_xfer_unsetup,
	.get_dma_delay = octusb_get_dma_delay,
	.device_resume = octusb_device_resume,
	.device_suspend = octusb_device_suspend,
	.set_hw_power = octusb_set_hw_power,
	.set_hw_power_sleep = octusb_set_hw_power_sleep,
	.roothub_exec = octusb_roothub_exec,
	.xfer_poll = octusb_do_poll,
};
