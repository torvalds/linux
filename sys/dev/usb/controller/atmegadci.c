/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Hans Petter Selasky. All rights reserved.
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
 * This file contains the driver for the ATMEGA series USB OTG Controller. This
 * driver currently only supports the DCI mode of the USB hardware.
 */

/*
 * NOTE: When the chip detects BUS-reset it will also reset the
 * endpoints, Function-address and more.
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

#define	USB_DEBUG_VAR atmegadci_debug

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
#endif			/* USB_GLOBAL_INCLUDE_FILE */

#include <dev/usb/controller/atmegadci.h>

#define	ATMEGA_BUS2SC(bus) \
   ((struct atmegadci_softc *)(((uint8_t *)(bus)) - \
    ((uint8_t *)&(((struct atmegadci_softc *)0)->sc_bus))))

#define	ATMEGA_PC2SC(pc) \
   ATMEGA_BUS2SC(USB_DMATAG_TO_XROOT((pc)->tag_parent)->bus)

#ifdef USB_DEBUG
static int atmegadci_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, atmegadci, CTLFLAG_RW, 0,
    "USB ATMEGA DCI");
SYSCTL_INT(_hw_usb_atmegadci, OID_AUTO, debug, CTLFLAG_RWTUN,
    &atmegadci_debug, 0, "ATMEGA DCI debug level");
#endif

#define	ATMEGA_INTR_ENDPT 1

/* prototypes */

static const struct usb_bus_methods atmegadci_bus_methods;
static const struct usb_pipe_methods atmegadci_device_non_isoc_methods;
static const struct usb_pipe_methods atmegadci_device_isoc_fs_methods;

static atmegadci_cmd_t atmegadci_setup_rx;
static atmegadci_cmd_t atmegadci_data_rx;
static atmegadci_cmd_t atmegadci_data_tx;
static atmegadci_cmd_t atmegadci_data_tx_sync;
static void atmegadci_device_done(struct usb_xfer *, usb_error_t);
static void atmegadci_do_poll(struct usb_bus *);
static void atmegadci_standard_done(struct usb_xfer *);
static void atmegadci_root_intr(struct atmegadci_softc *sc);

/*
 * Here is a list of what the chip supports:
 */
static const struct usb_hw_ep_profile
	atmegadci_ep_profile[2] = {

	[0] = {
		.max_in_frame_size = 64,
		.max_out_frame_size = 64,
		.is_simplex = 1,
		.support_control = 1,
	},
	[1] = {
		.max_in_frame_size = 64,
		.max_out_frame_size = 64,
		.is_simplex = 1,
		.support_bulk = 1,
		.support_interrupt = 1,
		.support_isochronous = 1,
		.support_in = 1,
		.support_out = 1,
	},
};

static void
atmegadci_get_hw_ep_profile(struct usb_device *udev,
    const struct usb_hw_ep_profile **ppf, uint8_t ep_addr)
{
	if (ep_addr == 0)
		*ppf = atmegadci_ep_profile;
	else if (ep_addr < ATMEGA_EP_MAX)
		*ppf = atmegadci_ep_profile + 1;
	else
		*ppf = NULL;
}

static void
atmegadci_clocks_on(struct atmegadci_softc *sc)
{
	if (sc->sc_flags.clocks_off &&
	    sc->sc_flags.port_powered) {

		DPRINTFN(5, "\n");

		/* turn on clocks */
		(sc->sc_clocks_on) (&sc->sc_bus);

		ATMEGA_WRITE_1(sc, ATMEGA_USBCON,
		    ATMEGA_USBCON_USBE |
		    ATMEGA_USBCON_OTGPADE |
		    ATMEGA_USBCON_VBUSTE);

		sc->sc_flags.clocks_off = 0;

		/* enable transceiver ? */
	}
}

static void
atmegadci_clocks_off(struct atmegadci_softc *sc)
{
	if (!sc->sc_flags.clocks_off) {

		DPRINTFN(5, "\n");

		/* disable Transceiver ? */

		ATMEGA_WRITE_1(sc, ATMEGA_USBCON,
		    ATMEGA_USBCON_USBE |
		    ATMEGA_USBCON_OTGPADE |
		    ATMEGA_USBCON_FRZCLK |
		    ATMEGA_USBCON_VBUSTE);

		/* turn clocks off */
		(sc->sc_clocks_off) (&sc->sc_bus);

		sc->sc_flags.clocks_off = 1;
	}
}

static void
atmegadci_pull_up(struct atmegadci_softc *sc)
{
	/* pullup D+, if possible */

	if (!sc->sc_flags.d_pulled_up &&
	    sc->sc_flags.port_powered) {
		sc->sc_flags.d_pulled_up = 1;
		ATMEGA_WRITE_1(sc, ATMEGA_UDCON, 0);
	}
}

static void
atmegadci_pull_down(struct atmegadci_softc *sc)
{
	/* pulldown D+, if possible */

	if (sc->sc_flags.d_pulled_up) {
		sc->sc_flags.d_pulled_up = 0;
		ATMEGA_WRITE_1(sc, ATMEGA_UDCON, ATMEGA_UDCON_DETACH);
	}
}

static void
atmegadci_wakeup_peer(struct atmegadci_softc *sc)
{
	uint8_t temp;

	if (!sc->sc_flags.status_suspend) {
		return;
	}

	temp = ATMEGA_READ_1(sc, ATMEGA_UDCON);
	ATMEGA_WRITE_1(sc, ATMEGA_UDCON, temp | ATMEGA_UDCON_RMWKUP);

	/* wait 8 milliseconds */
	/* Wait for reset to complete. */
	usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 125);

	/* hardware should have cleared RMWKUP bit */
}

static void
atmegadci_set_address(struct atmegadci_softc *sc, uint8_t addr)
{
	DPRINTFN(5, "addr=%d\n", addr);

	addr |= ATMEGA_UDADDR_ADDEN;

	ATMEGA_WRITE_1(sc, ATMEGA_UDADDR, addr);
}

static uint8_t
atmegadci_setup_rx(struct atmegadci_td *td)
{
	struct atmegadci_softc *sc;
	struct usb_device_request req;
	uint16_t count;
	uint8_t temp;

	/* get pointer to softc */
	sc = ATMEGA_PC2SC(td->pc);

	/* select endpoint number */
	ATMEGA_WRITE_1(sc, ATMEGA_UENUM, td->ep_no);

	/* check endpoint status */
	temp = ATMEGA_READ_1(sc, ATMEGA_UEINTX);

	DPRINTFN(5, "UEINTX=0x%02x\n", temp);

	if (!(temp & ATMEGA_UEINTX_RXSTPI)) {
		goto not_complete;
	}
	/* clear did stall */
	td->did_stall = 0;
	/* get the packet byte count */
	count =
	    (ATMEGA_READ_1(sc, ATMEGA_UEBCHX) << 8) |
	    (ATMEGA_READ_1(sc, ATMEGA_UEBCLX));

	/* mask away undefined bits */
	count &= 0x7FF;

	/* verify data length */
	if (count != td->remainder) {
		DPRINTFN(0, "Invalid SETUP packet "
		    "length, %d bytes\n", count);
		goto not_complete;
	}
	if (count != sizeof(req)) {
		DPRINTFN(0, "Unsupported SETUP packet "
		    "length, %d bytes\n", count);
		goto not_complete;
	}
	/* receive data */
	ATMEGA_READ_MULTI_1(sc, ATMEGA_UEDATX,
	    (void *)&req, sizeof(req));

	/* copy data into real buffer */
	usbd_copy_in(td->pc, 0, &req, sizeof(req));

	td->offset = sizeof(req);
	td->remainder = 0;

	/* sneak peek the set address */
	if ((req.bmRequestType == UT_WRITE_DEVICE) &&
	    (req.bRequest == UR_SET_ADDRESS)) {
		sc->sc_dv_addr = req.wValue[0] & 0x7F;
		/* must write address before ZLP */
		ATMEGA_WRITE_1(sc, ATMEGA_UDADDR, sc->sc_dv_addr);
	} else {
		sc->sc_dv_addr = 0xFF;
	}

	/* Clear SETUP packet interrupt and all other previous interrupts */
	ATMEGA_WRITE_1(sc, ATMEGA_UEINTX, 0);
	return (0);			/* complete */

not_complete:
	/* abort any ongoing transfer */
	if (!td->did_stall) {
		DPRINTFN(5, "stalling\n");
		ATMEGA_WRITE_1(sc, ATMEGA_UECONX,
		    ATMEGA_UECONX_EPEN |
		    ATMEGA_UECONX_STALLRQ);
		td->did_stall = 1;
	}
	if (temp & ATMEGA_UEINTX_RXSTPI) {
		/* clear SETUP packet interrupt */
		ATMEGA_WRITE_1(sc, ATMEGA_UEINTX, ~ATMEGA_UEINTX_RXSTPI);
	}
	/* we only want to know if there is a SETUP packet */
	ATMEGA_WRITE_1(sc, ATMEGA_UEIENX, ATMEGA_UEIENX_RXSTPE);
	return (1);			/* not complete */
}

static uint8_t
atmegadci_data_rx(struct atmegadci_td *td)
{
	struct atmegadci_softc *sc;
	struct usb_page_search buf_res;
	uint16_t count;
	uint8_t temp;
	uint8_t to;
	uint8_t got_short;

	to = 3;				/* don't loop forever! */
	got_short = 0;

	/* get pointer to softc */
	sc = ATMEGA_PC2SC(td->pc);

	/* select endpoint number */
	ATMEGA_WRITE_1(sc, ATMEGA_UENUM, td->ep_no);

repeat:
	/* check if any of the FIFO banks have data */
	/* check endpoint status */
	temp = ATMEGA_READ_1(sc, ATMEGA_UEINTX);

	DPRINTFN(5, "temp=0x%02x rem=%u\n", temp, td->remainder);

	if (temp & ATMEGA_UEINTX_RXSTPI) {
		if (td->remainder == 0) {
			/*
			 * We are actually complete and have
			 * received the next SETUP
			 */
			DPRINTFN(5, "faking complete\n");
			return (0);	/* complete */
		}
		/*
	         * USB Host Aborted the transfer.
	         */
		td->error = 1;
		return (0);		/* complete */
	}
	/* check status */
	if (!(temp & (ATMEGA_UEINTX_FIFOCON |
	    ATMEGA_UEINTX_RXOUTI))) {
		/* no data */
		goto not_complete;
	}
	/* get the packet byte count */
	count =
	    (ATMEGA_READ_1(sc, ATMEGA_UEBCHX) << 8) |
	    (ATMEGA_READ_1(sc, ATMEGA_UEBCLX));

	/* mask away undefined bits */
	count &= 0x7FF;

	/* verify the packet byte count */
	if (count != td->max_packet_size) {
		if (count < td->max_packet_size) {
			/* we have a short packet */
			td->short_pkt = 1;
			got_short = 1;
		} else {
			/* invalid USB packet */
			td->error = 1;
			return (0);	/* we are complete */
		}
	}
	/* verify the packet byte count */
	if (count > td->remainder) {
		/* invalid USB packet */
		td->error = 1;
		return (0);		/* we are complete */
	}
	while (count > 0) {
		usbd_get_page(td->pc, td->offset, &buf_res);

		/* get correct length */
		if (buf_res.length > count) {
			buf_res.length = count;
		}
		/* receive data */
		ATMEGA_READ_MULTI_1(sc, ATMEGA_UEDATX,
		    buf_res.buffer, buf_res.length);

		/* update counters */
		count -= buf_res.length;
		td->offset += buf_res.length;
		td->remainder -= buf_res.length;
	}

	/* clear OUT packet interrupt */
	ATMEGA_WRITE_1(sc, ATMEGA_UEINTX, ATMEGA_UEINTX_RXOUTI ^ 0xFF);

	/* release FIFO bank */
	ATMEGA_WRITE_1(sc, ATMEGA_UEINTX, ATMEGA_UEINTX_FIFOCON ^ 0xFF);

	/* check if we are complete */
	if ((td->remainder == 0) || got_short) {
		if (td->short_pkt) {
			/* we are complete */
			return (0);
		}
		/* else need to receive a zero length packet */
	}
	if (--to) {
		goto repeat;
	}
not_complete:
	/* we only want to know if there is a SETUP packet or OUT packet */
	ATMEGA_WRITE_1(sc, ATMEGA_UEIENX,
	    ATMEGA_UEIENX_RXSTPE | ATMEGA_UEIENX_RXOUTE);
	return (1);			/* not complete */
}

static uint8_t
atmegadci_data_tx(struct atmegadci_td *td)
{
	struct atmegadci_softc *sc;
	struct usb_page_search buf_res;
	uint16_t count;
	uint8_t to;
	uint8_t temp;

	to = 3;				/* don't loop forever! */

	/* get pointer to softc */
	sc = ATMEGA_PC2SC(td->pc);

	/* select endpoint number */
	ATMEGA_WRITE_1(sc, ATMEGA_UENUM, td->ep_no);

repeat:

	/* check endpoint status */
	temp = ATMEGA_READ_1(sc, ATMEGA_UEINTX);

	DPRINTFN(5, "temp=0x%02x rem=%u\n", temp, td->remainder);

	if (temp & ATMEGA_UEINTX_RXSTPI) {
		/*
	         * The current transfer was aborted
	         * by the USB Host
	         */
		td->error = 1;
		return (0);		/* complete */
	}

	temp = ATMEGA_READ_1(sc, ATMEGA_UESTA0X);
	if (temp & 3) {
		/* cannot write any data - a bank is busy */
		goto not_complete;
	}

	count = td->max_packet_size;
	if (td->remainder < count) {
		/* we have a short packet */
		td->short_pkt = 1;
		count = td->remainder;
	}
	while (count > 0) {

		usbd_get_page(td->pc, td->offset, &buf_res);

		/* get correct length */
		if (buf_res.length > count) {
			buf_res.length = count;
		}
		/* transmit data */
		ATMEGA_WRITE_MULTI_1(sc, ATMEGA_UEDATX,
		    buf_res.buffer, buf_res.length);

		/* update counters */
		count -= buf_res.length;
		td->offset += buf_res.length;
		td->remainder -= buf_res.length;
	}

	/* clear IN packet interrupt */
	ATMEGA_WRITE_1(sc, ATMEGA_UEINTX, 0xFF ^ ATMEGA_UEINTX_TXINI);

	/* allocate FIFO bank */
	ATMEGA_WRITE_1(sc, ATMEGA_UEINTX, 0xFF ^ ATMEGA_UEINTX_FIFOCON);

	/* check remainder */
	if (td->remainder == 0) {
		if (td->short_pkt) {
			return (0);	/* complete */
		}
		/* else we need to transmit a short packet */
	}
	if (--to) {
		goto repeat;
	}
not_complete:
	/* we only want to know if there is a SETUP packet or free IN packet */
	ATMEGA_WRITE_1(sc, ATMEGA_UEIENX,
	    ATMEGA_UEIENX_RXSTPE | ATMEGA_UEIENX_TXINE);
	return (1);			/* not complete */
}

static uint8_t
atmegadci_data_tx_sync(struct atmegadci_td *td)
{
	struct atmegadci_softc *sc;
	uint8_t temp;

	/* get pointer to softc */
	sc = ATMEGA_PC2SC(td->pc);

	/* select endpoint number */
	ATMEGA_WRITE_1(sc, ATMEGA_UENUM, td->ep_no);

	/* check endpoint status */
	temp = ATMEGA_READ_1(sc, ATMEGA_UEINTX);

	DPRINTFN(5, "temp=0x%02x\n", temp);

	if (temp & ATMEGA_UEINTX_RXSTPI) {
		DPRINTFN(5, "faking complete\n");
		/* Race condition */
		return (0);		/* complete */
	}
	/*
	 * The control endpoint has only got one bank, so if that bank
	 * is free the packet has been transferred!
	 */
	temp = ATMEGA_READ_1(sc, ATMEGA_UESTA0X);
	if (temp & 3) {
		/* cannot write any data - a bank is busy */
		goto not_complete;
	}
	if (sc->sc_dv_addr != 0xFF) {
		/* set new address */
		atmegadci_set_address(sc, sc->sc_dv_addr);
	}
	return (0);			/* complete */

not_complete:
	/* we only want to know if there is a SETUP packet or free IN packet */
	ATMEGA_WRITE_1(sc, ATMEGA_UEIENX,
	    ATMEGA_UEIENX_RXSTPE | ATMEGA_UEIENX_TXINE);
	return (1);			/* not complete */
}

static uint8_t
atmegadci_xfer_do_fifo(struct usb_xfer *xfer)
{
	struct atmegadci_td *td;

	DPRINTFN(9, "\n");

	td = xfer->td_transfer_cache;
	while (1) {
		if ((td->func) (td)) {
			/* operation in progress */
			break;
		}
		if (((void *)td) == xfer->td_transfer_last) {
			goto done;
		}
		if (td->error) {
			goto done;
		} else if (td->remainder > 0) {
			/*
			 * We had a short transfer. If there is no alternate
			 * next, stop processing !
			 */
			if (!td->alt_next) {
				goto done;
			}
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

	atmegadci_standard_done(xfer);
	return (0);			/* complete */
}

static void
atmegadci_interrupt_poll(struct atmegadci_softc *sc)
{
	struct usb_xfer *xfer;

repeat:
	TAILQ_FOREACH(xfer, &sc->sc_bus.intr_q.head, wait_entry) {
		if (!atmegadci_xfer_do_fifo(xfer)) {
			/* queue has been modified */
			goto repeat;
		}
	}
}

static void
atmegadci_vbus_interrupt(struct atmegadci_softc *sc, uint8_t is_on)
{
	DPRINTFN(5, "vbus = %u\n", is_on);

	if (is_on) {
		if (!sc->sc_flags.status_vbus) {
			sc->sc_flags.status_vbus = 1;

			/* complete root HUB interrupt endpoint */

			atmegadci_root_intr(sc);
		}
	} else {
		if (sc->sc_flags.status_vbus) {
			sc->sc_flags.status_vbus = 0;
			sc->sc_flags.status_bus_reset = 0;
			sc->sc_flags.status_suspend = 0;
			sc->sc_flags.change_suspend = 0;
			sc->sc_flags.change_connect = 1;

			/* complete root HUB interrupt endpoint */

			atmegadci_root_intr(sc);
		}
	}
}

void
atmegadci_interrupt(struct atmegadci_softc *sc)
{
	uint8_t status;

	USB_BUS_LOCK(&sc->sc_bus);

	/* read interrupt status */
	status = ATMEGA_READ_1(sc, ATMEGA_UDINT);

	/* clear all set interrupts */
	ATMEGA_WRITE_1(sc, ATMEGA_UDINT, (~status) & 0x7D);

	DPRINTFN(14, "UDINT=0x%02x\n", status);

	/* check for any bus state change interrupts */
	if (status & ATMEGA_UDINT_EORSTI) {

		DPRINTFN(5, "end of reset\n");

		/* set correct state */
		sc->sc_flags.status_bus_reset = 1;
		sc->sc_flags.status_suspend = 0;
		sc->sc_flags.change_suspend = 0;
		sc->sc_flags.change_connect = 1;

		/* disable resume interrupt */
		ATMEGA_WRITE_1(sc, ATMEGA_UDIEN,
		    ATMEGA_UDINT_SUSPE |
		    ATMEGA_UDINT_EORSTE);

		/* complete root HUB interrupt endpoint */
		atmegadci_root_intr(sc);
	}
	/*
	 * If resume and suspend is set at the same time we interpret
	 * that like RESUME. Resume is set when there is at least 3
	 * milliseconds of inactivity on the USB BUS.
	 */
	if (status & ATMEGA_UDINT_WAKEUPI) {

		DPRINTFN(5, "resume interrupt\n");

		if (sc->sc_flags.status_suspend) {
			/* update status bits */
			sc->sc_flags.status_suspend = 0;
			sc->sc_flags.change_suspend = 1;

			/* disable resume interrupt */
			ATMEGA_WRITE_1(sc, ATMEGA_UDIEN,
			    ATMEGA_UDINT_SUSPE |
			    ATMEGA_UDINT_EORSTE);

			/* complete root HUB interrupt endpoint */
			atmegadci_root_intr(sc);
		}
	} else if (status & ATMEGA_UDINT_SUSPI) {

		DPRINTFN(5, "suspend interrupt\n");

		if (!sc->sc_flags.status_suspend) {
			/* update status bits */
			sc->sc_flags.status_suspend = 1;
			sc->sc_flags.change_suspend = 1;

			/* disable suspend interrupt */
			ATMEGA_WRITE_1(sc, ATMEGA_UDIEN,
			    ATMEGA_UDINT_WAKEUPE |
			    ATMEGA_UDINT_EORSTE);

			/* complete root HUB interrupt endpoint */
			atmegadci_root_intr(sc);
		}
	}
	/* check VBUS */
	status = ATMEGA_READ_1(sc, ATMEGA_USBINT);

	/* clear all set interrupts */
	ATMEGA_WRITE_1(sc, ATMEGA_USBINT, (~status) & 0x03);

	if (status & ATMEGA_USBINT_VBUSTI) {
		uint8_t temp;

		DPRINTFN(5, "USBINT=0x%02x\n", status);

		temp = ATMEGA_READ_1(sc, ATMEGA_USBSTA);
		atmegadci_vbus_interrupt(sc, temp & ATMEGA_USBSTA_VBUS);
	}
	/* check for any endpoint interrupts */
	status = ATMEGA_READ_1(sc, ATMEGA_UEINT);
	/* the hardware will clear the UEINT bits automatically */
	if (status) {

		DPRINTFN(5, "real endpoint interrupt UEINT=0x%02x\n", status);

		atmegadci_interrupt_poll(sc);
	}
	USB_BUS_UNLOCK(&sc->sc_bus);
}

static void
atmegadci_setup_standard_chain_sub(struct atmegadci_std_temp *temp)
{
	struct atmegadci_td *td;

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
	td->error = 0;
	td->did_stall = temp->did_stall;
	td->short_pkt = temp->short_pkt;
	td->alt_next = temp->setup_alt_next;
}

static void
atmegadci_setup_standard_chain(struct usb_xfer *xfer)
{
	struct atmegadci_std_temp temp;
	struct atmegadci_softc *sc;
	struct atmegadci_td *td;
	uint32_t x;
	uint8_t ep_no;
	uint8_t need_sync;

	DPRINTFN(9, "addr=%d endpt=%d sumlen=%d speed=%d\n",
	    xfer->address, UE_GET_ADDR(xfer->endpointno),
	    xfer->sumlen, usbd_get_speed(xfer->xroot->udev));

	temp.max_frame_size = xfer->max_frame_size;

	td = xfer->td_start[0];
	xfer->td_transfer_first = td;
	xfer->td_transfer_cache = td;

	/* setup temp */

	temp.pc = NULL;
	temp.td = NULL;
	temp.td_next = xfer->td_start[0];
	temp.offset = 0;
	temp.setup_alt_next = xfer->flags_int.short_frames_ok ||
	    xfer->flags_int.isochronous_xfr;
	temp.did_stall = !xfer->flags_int.control_stall;

	sc = ATMEGA_BUS2SC(xfer->xroot->bus);
	ep_no = (xfer->endpointno & UE_ADDR);

	/* check if we should prepend a setup message */

	if (xfer->flags_int.control_xfr) {
		if (xfer->flags_int.control_hdr) {

			temp.func = &atmegadci_setup_rx;
			temp.len = xfer->frlengths[0];
			temp.pc = xfer->frbuffers + 0;
			temp.short_pkt = temp.len ? 1 : 0;
			/* check for last frame */
			if (xfer->nframes == 1) {
				/* no STATUS stage yet, SETUP is last */
				if (xfer->flags_int.control_act)
					temp.setup_alt_next = 0;
			}

			atmegadci_setup_standard_chain_sub(&temp);
		}
		x = 1;
	} else {
		x = 0;
	}

	if (x != xfer->nframes) {
		if (xfer->endpointno & UE_DIR_IN) {
			temp.func = &atmegadci_data_tx;
			need_sync = 1;
		} else {
			temp.func = &atmegadci_data_rx;
			need_sync = 0;
		}

		/* setup "pc" pointer */
		temp.pc = xfer->frbuffers + x;
	} else {
		need_sync = 0;
	}
	while (x != xfer->nframes) {

		/* DATA0 / DATA1 message */

		temp.len = xfer->frlengths[x];

		x++;

		if (x == xfer->nframes) {
			if (xfer->flags_int.control_xfr) {
				if (xfer->flags_int.control_act) {
					temp.setup_alt_next = 0;
				}
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

		atmegadci_setup_standard_chain_sub(&temp);

		if (xfer->flags_int.isochronous_xfr) {
			temp.offset += temp.len;
		} else {
			/* get next Page Cache pointer */
			temp.pc = xfer->frbuffers + x;
		}
	}

	if (xfer->flags_int.control_xfr) {

		/* always setup a valid "pc" pointer for status and sync */
		temp.pc = xfer->frbuffers + 0;
		temp.len = 0;
		temp.short_pkt = 0;
		temp.setup_alt_next = 0;

		/* check if we need to sync */
		if (need_sync) {
			/* we need a SYNC point after TX */
			temp.func = &atmegadci_data_tx_sync;
			atmegadci_setup_standard_chain_sub(&temp);
		}

		/* check if we should append a status stage */
		if (!xfer->flags_int.control_act) {

			/*
			 * Send a DATA1 message and invert the current
			 * endpoint direction.
			 */
			if (xfer->endpointno & UE_DIR_IN) {
				temp.func = &atmegadci_data_rx;
				need_sync = 0;
			} else {
				temp.func = &atmegadci_data_tx;
				need_sync = 1;
			}

			atmegadci_setup_standard_chain_sub(&temp);
			if (need_sync) {
				/* we need a SYNC point after TX */
				temp.func = &atmegadci_data_tx_sync;
				atmegadci_setup_standard_chain_sub(&temp);
			}
		}
	}
	/* must have at least one frame! */
	td = temp.td;
	xfer->td_transfer_last = td;
}

static void
atmegadci_timeout(void *arg)
{
	struct usb_xfer *xfer = arg;

	DPRINTF("xfer=%p\n", xfer);

	USB_BUS_LOCK_ASSERT(xfer->xroot->bus, MA_OWNED);

	/* transfer is transferred */
	atmegadci_device_done(xfer, USB_ERR_TIMEOUT);
}

static void
atmegadci_start_standard_chain(struct usb_xfer *xfer)
{
	DPRINTFN(9, "\n");

	/* poll one time - will turn on interrupts */
	if (atmegadci_xfer_do_fifo(xfer)) {

		/* put transfer on interrupt queue */
		usbd_transfer_enqueue(&xfer->xroot->bus->intr_q, xfer);

		/* start timeout, if any */
		if (xfer->timeout != 0) {
			usbd_transfer_timeout_ms(xfer,
			    &atmegadci_timeout, xfer->timeout);
		}
	}
}

static void
atmegadci_root_intr(struct atmegadci_softc *sc)
{
	DPRINTFN(9, "\n");

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	/* set port bit */
	sc->sc_hub_idata[0] = 0x02;	/* we only have one port */

	uhub_root_intr(&sc->sc_bus, sc->sc_hub_idata,
	    sizeof(sc->sc_hub_idata));
 }

static usb_error_t
atmegadci_standard_done_sub(struct usb_xfer *xfer)
{
	struct atmegadci_td *td;
	uint32_t len;
	uint8_t error;

	DPRINTFN(9, "\n");

	td = xfer->td_transfer_cache;

	do {
		len = td->remainder;

		if (xfer->aframes != xfer->nframes) {
			/*
		         * Verify the length and subtract
		         * the remainder from "frlengths[]":
		         */
			if (len > xfer->frlengths[xfer->aframes]) {
				td->error = 1;
			} else {
				xfer->frlengths[xfer->aframes] -= len;
			}
		}
		/* Check for transfer error */
		if (td->error) {
			/* the transfer is finished */
			error = 1;
			td = NULL;
			break;
		}
		/* Check for short transfer */
		if (len > 0) {
			if (xfer->flags_int.short_frames_ok ||
			    xfer->flags_int.isochronous_xfr) {
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

	return (error ?
	    USB_ERR_STALLED : USB_ERR_NORMAL_COMPLETION);
}

static void
atmegadci_standard_done(struct usb_xfer *xfer)
{
	usb_error_t err = 0;

	DPRINTFN(13, "xfer=%p endpoint=%p transfer done\n",
	    xfer, xfer->endpoint);

	/* reset scanner */

	xfer->td_transfer_cache = xfer->td_transfer_first;

	if (xfer->flags_int.control_xfr) {

		if (xfer->flags_int.control_hdr) {

			err = atmegadci_standard_done_sub(xfer);
		}
		xfer->aframes = 1;

		if (xfer->td_transfer_cache == NULL) {
			goto done;
		}
	}
	while (xfer->aframes != xfer->nframes) {

		err = atmegadci_standard_done_sub(xfer);
		xfer->aframes++;

		if (xfer->td_transfer_cache == NULL) {
			goto done;
		}
	}

	if (xfer->flags_int.control_xfr &&
	    !xfer->flags_int.control_act) {

		err = atmegadci_standard_done_sub(xfer);
	}
done:
	atmegadci_device_done(xfer, err);
}

/*------------------------------------------------------------------------*
 *	atmegadci_device_done
 *
 * NOTE: this function can be called more than one time on the
 * same USB transfer!
 *------------------------------------------------------------------------*/
static void
atmegadci_device_done(struct usb_xfer *xfer, usb_error_t error)
{
	struct atmegadci_softc *sc = ATMEGA_BUS2SC(xfer->xroot->bus);
	uint8_t ep_no;

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	DPRINTFN(9, "xfer=%p, endpoint=%p, error=%d\n",
	    xfer, xfer->endpoint, error);

	if (xfer->flags_int.usb_mode == USB_MODE_DEVICE) {
		ep_no = (xfer->endpointno & UE_ADDR);

		/* select endpoint number */
		ATMEGA_WRITE_1(sc, ATMEGA_UENUM, ep_no);

		/* disable endpoint interrupt */
		ATMEGA_WRITE_1(sc, ATMEGA_UEIENX, 0);

		DPRINTFN(15, "disabled interrupts!\n");
	}
	/* dequeue transfer and start next transfer */
	usbd_transfer_done(xfer, error);
}

static void
atmegadci_xfer_stall(struct usb_xfer *xfer)
{
	atmegadci_device_done(xfer, USB_ERR_STALLED);
}

static void
atmegadci_set_stall(struct usb_device *udev,
    struct usb_endpoint *ep, uint8_t *did_stall)
{
	struct atmegadci_softc *sc;
	uint8_t ep_no;

	USB_BUS_LOCK_ASSERT(udev->bus, MA_OWNED);

	DPRINTFN(5, "endpoint=%p\n", ep);

	sc = ATMEGA_BUS2SC(udev->bus);
	/* get endpoint number */
	ep_no = (ep->edesc->bEndpointAddress & UE_ADDR);
	/* select endpoint number */
	ATMEGA_WRITE_1(sc, ATMEGA_UENUM, ep_no);
	/* set stall */
	ATMEGA_WRITE_1(sc, ATMEGA_UECONX,
	    ATMEGA_UECONX_EPEN |
	    ATMEGA_UECONX_STALLRQ);
}

static void
atmegadci_clear_stall_sub(struct atmegadci_softc *sc, uint8_t ep_no,
    uint8_t ep_type, uint8_t ep_dir)
{
	uint8_t temp;

	if (ep_type == UE_CONTROL) {
		/* clearing stall is not needed */
		return;
	}
	/* select endpoint number */
	ATMEGA_WRITE_1(sc, ATMEGA_UENUM, ep_no);

	/* set endpoint reset */
	ATMEGA_WRITE_1(sc, ATMEGA_UERST, ATMEGA_UERST_MASK(ep_no));

	/* clear endpoint reset */
	ATMEGA_WRITE_1(sc, ATMEGA_UERST, 0);

	/* set stall */
	ATMEGA_WRITE_1(sc, ATMEGA_UECONX,
	    ATMEGA_UECONX_EPEN |
	    ATMEGA_UECONX_STALLRQ);

	/* reset data toggle */
	ATMEGA_WRITE_1(sc, ATMEGA_UECONX,
	    ATMEGA_UECONX_EPEN |
	    ATMEGA_UECONX_RSTDT);

	/* clear stall */
	ATMEGA_WRITE_1(sc, ATMEGA_UECONX,
	    ATMEGA_UECONX_EPEN |
	    ATMEGA_UECONX_STALLRQC);

	do {
		if (ep_type == UE_BULK) {
			temp = ATMEGA_UECFG0X_EPTYPE2;
		} else if (ep_type == UE_INTERRUPT) {
			temp = ATMEGA_UECFG0X_EPTYPE3;
		} else {
			temp = ATMEGA_UECFG0X_EPTYPE1;
		}
		if (ep_dir & UE_DIR_IN) {
			temp |= ATMEGA_UECFG0X_EPDIR;
		}
		/* two banks, 64-bytes wMaxPacket */
		ATMEGA_WRITE_1(sc, ATMEGA_UECFG0X, temp);
		ATMEGA_WRITE_1(sc, ATMEGA_UECFG1X,
		    ATMEGA_UECFG1X_ALLOC |
		    ATMEGA_UECFG1X_EPBK0 |	/* one bank */
		    ATMEGA_UECFG1X_EPSIZE(3));

		temp = ATMEGA_READ_1(sc, ATMEGA_UESTA0X);
		if (!(temp & ATMEGA_UESTA0X_CFGOK)) {
			device_printf(sc->sc_bus.bdev,
			    "Chip rejected configuration\n");
		}
	} while (0);
}

static void
atmegadci_clear_stall(struct usb_device *udev, struct usb_endpoint *ep)
{
	struct atmegadci_softc *sc;
	struct usb_endpoint_descriptor *ed;

	DPRINTFN(5, "endpoint=%p\n", ep);

	USB_BUS_LOCK_ASSERT(udev->bus, MA_OWNED);

	/* check mode */
	if (udev->flags.usb_mode != USB_MODE_DEVICE) {
		/* not supported */
		return;
	}
	/* get softc */
	sc = ATMEGA_BUS2SC(udev->bus);

	/* get endpoint descriptor */
	ed = ep->edesc;

	/* reset endpoint */
	atmegadci_clear_stall_sub(sc,
	    (ed->bEndpointAddress & UE_ADDR),
	    (ed->bmAttributes & UE_XFERTYPE),
	    (ed->bEndpointAddress & (UE_DIR_IN | UE_DIR_OUT)));
}

usb_error_t
atmegadci_init(struct atmegadci_softc *sc)
{
	uint8_t n;

	DPRINTF("start\n");

	/* set up the bus structure */
	sc->sc_bus.usbrev = USB_REV_1_1;
	sc->sc_bus.methods = &atmegadci_bus_methods;

	USB_BUS_LOCK(&sc->sc_bus);

	/* make sure USB is enabled */
	ATMEGA_WRITE_1(sc, ATMEGA_USBCON,
	    ATMEGA_USBCON_USBE |
	    ATMEGA_USBCON_FRZCLK);

	/* enable USB PAD regulator */
	ATMEGA_WRITE_1(sc, ATMEGA_UHWCON,
	    ATMEGA_UHWCON_UVREGE |
	    ATMEGA_UHWCON_UIMOD);

	/* the following register sets up the USB PLL, assuming 16MHz X-tal */
	ATMEGA_WRITE_1(sc, 0x49 /* PLLCSR */, 0x14 | 0x02);

	/* wait for PLL to lock */
	for (n = 0; n != 20; n++) {
		if (ATMEGA_READ_1(sc, 0x49) & 0x01)
			break;
		/* wait a little bit for PLL to start */
		usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 100);
	}

	/* make sure USB is enabled */
	ATMEGA_WRITE_1(sc, ATMEGA_USBCON,
	    ATMEGA_USBCON_USBE |
	    ATMEGA_USBCON_OTGPADE |
	    ATMEGA_USBCON_VBUSTE);

	/* turn on clocks */
	(sc->sc_clocks_on) (&sc->sc_bus);

	/* make sure device is re-enumerated */
	ATMEGA_WRITE_1(sc, ATMEGA_UDCON, ATMEGA_UDCON_DETACH);

	/* wait a little for things to stabilise */
	usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 20);

	/* enable interrupts */
	ATMEGA_WRITE_1(sc, ATMEGA_UDIEN,
	    ATMEGA_UDINT_SUSPE |
	    ATMEGA_UDINT_EORSTE);

	/* reset all endpoints */
	ATMEGA_WRITE_1(sc, ATMEGA_UERST,
	    (1 << ATMEGA_EP_MAX) - 1);

	/* disable reset */
	ATMEGA_WRITE_1(sc, ATMEGA_UERST, 0);

	/* disable all endpoints */
	for (n = 0; n != ATMEGA_EP_MAX; n++) {

		/* select endpoint */
		ATMEGA_WRITE_1(sc, ATMEGA_UENUM, n);

		/* disable endpoint interrupt */
		ATMEGA_WRITE_1(sc, ATMEGA_UEIENX, 0);

		/* disable endpoint */
		ATMEGA_WRITE_1(sc, ATMEGA_UECONX, 0);
	}

	/* turn off clocks */

	atmegadci_clocks_off(sc);

	/* read initial VBUS state */

	n = ATMEGA_READ_1(sc, ATMEGA_USBSTA);
	atmegadci_vbus_interrupt(sc, n & ATMEGA_USBSTA_VBUS);

	USB_BUS_UNLOCK(&sc->sc_bus);

	/* catch any lost interrupts */

	atmegadci_do_poll(&sc->sc_bus);

	return (0);			/* success */
}

void
atmegadci_uninit(struct atmegadci_softc *sc)
{
	USB_BUS_LOCK(&sc->sc_bus);

	/* turn on clocks */
	(sc->sc_clocks_on) (&sc->sc_bus);

	/* disable interrupts */
	ATMEGA_WRITE_1(sc, ATMEGA_UDIEN, 0);

	/* reset all endpoints */
	ATMEGA_WRITE_1(sc, ATMEGA_UERST,
	    (1 << ATMEGA_EP_MAX) - 1);

	/* disable reset */
	ATMEGA_WRITE_1(sc, ATMEGA_UERST, 0);

	sc->sc_flags.port_powered = 0;
	sc->sc_flags.status_vbus = 0;
	sc->sc_flags.status_bus_reset = 0;
	sc->sc_flags.status_suspend = 0;
	sc->sc_flags.change_suspend = 0;
	sc->sc_flags.change_connect = 1;

	atmegadci_pull_down(sc);
	atmegadci_clocks_off(sc);

	/* disable USB PAD regulator */
	ATMEGA_WRITE_1(sc, ATMEGA_UHWCON, 0);

	USB_BUS_UNLOCK(&sc->sc_bus);
}

static void
atmegadci_suspend(struct atmegadci_softc *sc)
{
	/* TODO */
}

static void
atmegadci_resume(struct atmegadci_softc *sc)
{
	/* TODO */
}

static void
atmegadci_do_poll(struct usb_bus *bus)
{
	struct atmegadci_softc *sc = ATMEGA_BUS2SC(bus);

	USB_BUS_LOCK(&sc->sc_bus);
	atmegadci_interrupt_poll(sc);
	USB_BUS_UNLOCK(&sc->sc_bus);
}

/*------------------------------------------------------------------------*
 * atmegadci bulk support
 * atmegadci control support
 * atmegadci interrupt support
 *------------------------------------------------------------------------*/
static void
atmegadci_device_non_isoc_open(struct usb_xfer *xfer)
{
	return;
}

static void
atmegadci_device_non_isoc_close(struct usb_xfer *xfer)
{
	atmegadci_device_done(xfer, USB_ERR_CANCELLED);
}

static void
atmegadci_device_non_isoc_enter(struct usb_xfer *xfer)
{
	return;
}

static void
atmegadci_device_non_isoc_start(struct usb_xfer *xfer)
{
	/* setup TDs */
	atmegadci_setup_standard_chain(xfer);
	atmegadci_start_standard_chain(xfer);
}

static const struct usb_pipe_methods atmegadci_device_non_isoc_methods =
{
	.open = atmegadci_device_non_isoc_open,
	.close = atmegadci_device_non_isoc_close,
	.enter = atmegadci_device_non_isoc_enter,
	.start = atmegadci_device_non_isoc_start,
};

/*------------------------------------------------------------------------*
 * atmegadci full speed isochronous support
 *------------------------------------------------------------------------*/
static void
atmegadci_device_isoc_fs_open(struct usb_xfer *xfer)
{
	return;
}

static void
atmegadci_device_isoc_fs_close(struct usb_xfer *xfer)
{
	atmegadci_device_done(xfer, USB_ERR_CANCELLED);
}

static void
atmegadci_device_isoc_fs_enter(struct usb_xfer *xfer)
{
	struct atmegadci_softc *sc = ATMEGA_BUS2SC(xfer->xroot->bus);
	uint32_t temp;
	uint32_t nframes;

	DPRINTFN(6, "xfer=%p next=%d nframes=%d\n",
	    xfer, xfer->endpoint->isoc_next, xfer->nframes);

	/* get the current frame index */

	nframes =
	    (ATMEGA_READ_1(sc, ATMEGA_UDFNUMH) << 8) |
	    (ATMEGA_READ_1(sc, ATMEGA_UDFNUML));

	nframes &= ATMEGA_FRAME_MASK;

	/*
	 * check if the frame index is within the window where the frames
	 * will be inserted
	 */
	temp = (nframes - xfer->endpoint->isoc_next) & ATMEGA_FRAME_MASK;

	if ((xfer->endpoint->is_synced == 0) ||
	    (temp < xfer->nframes)) {
		/*
		 * If there is data underflow or the pipe queue is
		 * empty we schedule the transfer a few frames ahead
		 * of the current frame position. Else two isochronous
		 * transfers might overlap.
		 */
		xfer->endpoint->isoc_next = (nframes + 3) & ATMEGA_FRAME_MASK;
		xfer->endpoint->is_synced = 1;
		DPRINTFN(3, "start next=%d\n", xfer->endpoint->isoc_next);
	}
	/*
	 * compute how many milliseconds the insertion is ahead of the
	 * current frame position:
	 */
	temp = (xfer->endpoint->isoc_next - nframes) & ATMEGA_FRAME_MASK;

	/*
	 * pre-compute when the isochronous transfer will be finished:
	 */
	xfer->isoc_time_complete =
	    usb_isoc_time_expand(&sc->sc_bus, nframes) + temp +
	    xfer->nframes;

	/* compute frame number for next insertion */
	xfer->endpoint->isoc_next += xfer->nframes;

	/* setup TDs */
	atmegadci_setup_standard_chain(xfer);
}

static void
atmegadci_device_isoc_fs_start(struct usb_xfer *xfer)
{
	/* start TD chain */
	atmegadci_start_standard_chain(xfer);
}

static const struct usb_pipe_methods atmegadci_device_isoc_fs_methods =
{
	.open = atmegadci_device_isoc_fs_open,
	.close = atmegadci_device_isoc_fs_close,
	.enter = atmegadci_device_isoc_fs_enter,
	.start = atmegadci_device_isoc_fs_start,
};

/*------------------------------------------------------------------------*
 * atmegadci root control support
 *------------------------------------------------------------------------*
 * Simulate a hardware HUB by handling all the necessary requests.
 *------------------------------------------------------------------------*/

static const struct usb_device_descriptor atmegadci_devd = {
	.bLength = sizeof(struct usb_device_descriptor),
	.bDescriptorType = UDESC_DEVICE,
	.bcdUSB = {0x00, 0x02},
	.bDeviceClass = UDCLASS_HUB,
	.bDeviceSubClass = UDSUBCLASS_HUB,
	.bDeviceProtocol = UDPROTO_FSHUB,
	.bMaxPacketSize = 64,
	.bcdDevice = {0x00, 0x01},
	.iManufacturer = 1,
	.iProduct = 2,
	.bNumConfigurations = 1,
};

static const struct atmegadci_config_desc atmegadci_confd = {
	.confd = {
		.bLength = sizeof(struct usb_config_descriptor),
		.bDescriptorType = UDESC_CONFIG,
		.wTotalLength[0] = sizeof(atmegadci_confd),
		.bNumInterface = 1,
		.bConfigurationValue = 1,
		.iConfiguration = 0,
		.bmAttributes = UC_SELF_POWERED,
		.bMaxPower = 0,
	},
	.ifcd = {
		.bLength = sizeof(struct usb_interface_descriptor),
		.bDescriptorType = UDESC_INTERFACE,
		.bNumEndpoints = 1,
		.bInterfaceClass = UICLASS_HUB,
		.bInterfaceSubClass = UISUBCLASS_HUB,
		.bInterfaceProtocol = 0,
	},
	.endpd = {
		.bLength = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType = UDESC_ENDPOINT,
		.bEndpointAddress = (UE_DIR_IN | ATMEGA_INTR_ENDPT),
		.bmAttributes = UE_INTERRUPT,
		.wMaxPacketSize[0] = 8,
		.bInterval = 255,
	},
};

#define	HSETW(ptr, val) ptr = { (uint8_t)(val), (uint8_t)((val) >> 8) }

static const struct usb_hub_descriptor_min atmegadci_hubd = {
	.bDescLength = sizeof(atmegadci_hubd),
	.bDescriptorType = UDESC_HUB,
	.bNbrPorts = 1,
	HSETW(.wHubCharacteristics, (UHD_PWR_NO_SWITCH | UHD_OC_INDIVIDUAL)),
	.bPwrOn2PwrGood = 50,
	.bHubContrCurrent = 0,
	.DeviceRemovable = {0},		/* port is removable */
};

#define	STRING_VENDOR \
  "A\0T\0M\0E\0G\0A"

#define	STRING_PRODUCT \
  "D\0C\0I\0 \0R\0o\0o\0t\0 \0H\0U\0B"

USB_MAKE_STRING_DESC(STRING_VENDOR, atmegadci_vendor);
USB_MAKE_STRING_DESC(STRING_PRODUCT, atmegadci_product);

static usb_error_t
atmegadci_roothub_exec(struct usb_device *udev,
    struct usb_device_request *req, const void **pptr, uint16_t *plength)
{
	struct atmegadci_softc *sc = ATMEGA_BUS2SC(udev->bus);
	const void *ptr;
	uint16_t len;
	uint16_t value;
	uint16_t index;
	uint8_t temp;
	usb_error_t err;

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	/* buffer reset */
	ptr = (const void *)&sc->sc_hub_temp;
	len = 0;
	err = 0;

	value = UGETW(req->wValue);
	index = UGETW(req->wIndex);

	/* demultiplex the control request */

	switch (req->bmRequestType) {
	case UT_READ_DEVICE:
		switch (req->bRequest) {
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
		switch (req->bRequest) {
		case UR_SET_ADDRESS:
			goto tr_handle_set_address;
		case UR_SET_CONFIG:
			goto tr_handle_set_config;
		case UR_CLEAR_FEATURE:
			goto tr_valid;	/* nop */
		case UR_SET_DESCRIPTOR:
			goto tr_valid;	/* nop */
		case UR_SET_FEATURE:
		default:
			goto tr_stalled;
		}
		break;

	case UT_WRITE_ENDPOINT:
		switch (req->bRequest) {
		case UR_CLEAR_FEATURE:
			switch (UGETW(req->wValue)) {
			case UF_ENDPOINT_HALT:
				goto tr_handle_clear_halt;
			case UF_DEVICE_REMOTE_WAKEUP:
				goto tr_handle_clear_wakeup;
			default:
				goto tr_stalled;
			}
			break;
		case UR_SET_FEATURE:
			switch (UGETW(req->wValue)) {
			case UF_ENDPOINT_HALT:
				goto tr_handle_set_halt;
			case UF_DEVICE_REMOTE_WAKEUP:
				goto tr_handle_set_wakeup;
			default:
				goto tr_stalled;
			}
			break;
		case UR_SYNCH_FRAME:
			goto tr_valid;	/* nop */
		default:
			goto tr_stalled;
		}
		break;

	case UT_READ_ENDPOINT:
		switch (req->bRequest) {
		case UR_GET_STATUS:
			goto tr_handle_get_ep_status;
		default:
			goto tr_stalled;
		}
		break;

	case UT_WRITE_INTERFACE:
		switch (req->bRequest) {
		case UR_SET_INTERFACE:
			goto tr_handle_set_interface;
		case UR_CLEAR_FEATURE:
			goto tr_valid;	/* nop */
		case UR_SET_FEATURE:
		default:
			goto tr_stalled;
		}
		break;

	case UT_READ_INTERFACE:
		switch (req->bRequest) {
		case UR_GET_INTERFACE:
			goto tr_handle_get_interface;
		case UR_GET_STATUS:
			goto tr_handle_get_iface_status;
		default:
			goto tr_stalled;
		}
		break;

	case UT_WRITE_CLASS_INTERFACE:
	case UT_WRITE_VENDOR_INTERFACE:
		/* XXX forward */
		break;

	case UT_READ_CLASS_INTERFACE:
	case UT_READ_VENDOR_INTERFACE:
		/* XXX forward */
		break;

	case UT_WRITE_CLASS_DEVICE:
		switch (req->bRequest) {
		case UR_CLEAR_FEATURE:
			goto tr_valid;
		case UR_SET_DESCRIPTOR:
		case UR_SET_FEATURE:
			break;
		default:
			goto tr_stalled;
		}
		break;

	case UT_WRITE_CLASS_OTHER:
		switch (req->bRequest) {
		case UR_CLEAR_FEATURE:
			goto tr_handle_clear_port_feature;
		case UR_SET_FEATURE:
			goto tr_handle_set_port_feature;
		case UR_CLEAR_TT_BUFFER:
		case UR_RESET_TT:
		case UR_STOP_TT:
			goto tr_valid;

		default:
			goto tr_stalled;
		}
		break;

	case UT_READ_CLASS_OTHER:
		switch (req->bRequest) {
		case UR_GET_TT_STATE:
			goto tr_handle_get_tt_state;
		case UR_GET_STATUS:
			goto tr_handle_get_port_status;
		default:
			goto tr_stalled;
		}
		break;

	case UT_READ_CLASS_DEVICE:
		switch (req->bRequest) {
		case UR_GET_DESCRIPTOR:
			goto tr_handle_get_class_descriptor;
		case UR_GET_STATUS:
			goto tr_handle_get_class_status;

		default:
			goto tr_stalled;
		}
		break;
	default:
		goto tr_stalled;
	}
	goto tr_valid;

tr_handle_get_descriptor:
	switch (value >> 8) {
	case UDESC_DEVICE:
		if (value & 0xff) {
			goto tr_stalled;
		}
		len = sizeof(atmegadci_devd);
		ptr = (const void *)&atmegadci_devd;
		goto tr_valid;
	case UDESC_CONFIG:
		if (value & 0xff) {
			goto tr_stalled;
		}
		len = sizeof(atmegadci_confd);
		ptr = (const void *)&atmegadci_confd;
		goto tr_valid;
	case UDESC_STRING:
		switch (value & 0xff) {
		case 0:		/* Language table */
			len = sizeof(usb_string_lang_en);
			ptr = (const void *)&usb_string_lang_en;
			goto tr_valid;

		case 1:		/* Vendor */
			len = sizeof(atmegadci_vendor);
			ptr = (const void *)&atmegadci_vendor;
			goto tr_valid;

		case 2:		/* Product */
			len = sizeof(atmegadci_product);
			ptr = (const void *)&atmegadci_product;
			goto tr_valid;
		default:
			break;
		}
		break;
	default:
		goto tr_stalled;
	}
	goto tr_stalled;

tr_handle_get_config:
	len = 1;
	sc->sc_hub_temp.wValue[0] = sc->sc_conf;
	goto tr_valid;

tr_handle_get_status:
	len = 2;
	USETW(sc->sc_hub_temp.wValue, UDS_SELF_POWERED);
	goto tr_valid;

tr_handle_set_address:
	if (value & 0xFF00) {
		goto tr_stalled;
	}
	sc->sc_rt_addr = value;
	goto tr_valid;

tr_handle_set_config:
	if (value >= 2) {
		goto tr_stalled;
	}
	sc->sc_conf = value;
	goto tr_valid;

tr_handle_get_interface:
	len = 1;
	sc->sc_hub_temp.wValue[0] = 0;
	goto tr_valid;

tr_handle_get_tt_state:
tr_handle_get_class_status:
tr_handle_get_iface_status:
tr_handle_get_ep_status:
	len = 2;
	USETW(sc->sc_hub_temp.wValue, 0);
	goto tr_valid;

tr_handle_set_halt:
tr_handle_set_interface:
tr_handle_set_wakeup:
tr_handle_clear_wakeup:
tr_handle_clear_halt:
	goto tr_valid;

tr_handle_clear_port_feature:
	if (index != 1) {
		goto tr_stalled;
	}
	DPRINTFN(9, "UR_CLEAR_PORT_FEATURE on port %d\n", index);

	switch (value) {
	case UHF_PORT_SUSPEND:
		atmegadci_wakeup_peer(sc);
		break;

	case UHF_PORT_ENABLE:
		sc->sc_flags.port_enabled = 0;
		break;

	case UHF_PORT_TEST:
	case UHF_PORT_INDICATOR:
	case UHF_C_PORT_ENABLE:
	case UHF_C_PORT_OVER_CURRENT:
	case UHF_C_PORT_RESET:
		/* nops */
		break;
	case UHF_PORT_POWER:
		sc->sc_flags.port_powered = 0;
		atmegadci_pull_down(sc);
		atmegadci_clocks_off(sc);
		break;
	case UHF_C_PORT_CONNECTION:
		/* clear connect change flag */
		sc->sc_flags.change_connect = 0;

		if (!sc->sc_flags.status_bus_reset) {
			/* we are not connected */
			break;
		}

		/* configure the control endpoint */

		/* select endpoint number */
		ATMEGA_WRITE_1(sc, ATMEGA_UENUM, 0);

		/* set endpoint reset */
		ATMEGA_WRITE_1(sc, ATMEGA_UERST, ATMEGA_UERST_MASK(0));

		/* clear endpoint reset */
		ATMEGA_WRITE_1(sc, ATMEGA_UERST, 0);

		/* enable and stall endpoint */
		ATMEGA_WRITE_1(sc, ATMEGA_UECONX,
		    ATMEGA_UECONX_EPEN |
		    ATMEGA_UECONX_STALLRQ);

		/* one bank, 64-bytes wMaxPacket */
		ATMEGA_WRITE_1(sc, ATMEGA_UECFG0X,
		    ATMEGA_UECFG0X_EPTYPE0);
		ATMEGA_WRITE_1(sc, ATMEGA_UECFG1X,
		    ATMEGA_UECFG1X_ALLOC |
		    ATMEGA_UECFG1X_EPBK0 |
		    ATMEGA_UECFG1X_EPSIZE(3));

		/* check valid config */
		temp = ATMEGA_READ_1(sc, ATMEGA_UESTA0X);
		if (!(temp & ATMEGA_UESTA0X_CFGOK)) {
			device_printf(sc->sc_bus.bdev,
			    "Chip rejected EP0 configuration\n");
		}
		break;
	case UHF_C_PORT_SUSPEND:
		sc->sc_flags.change_suspend = 0;
		break;
	default:
		err = USB_ERR_IOERROR;
		goto done;
	}
	goto tr_valid;

tr_handle_set_port_feature:
	if (index != 1) {
		goto tr_stalled;
	}
	DPRINTFN(9, "UR_SET_PORT_FEATURE\n");

	switch (value) {
	case UHF_PORT_ENABLE:
		sc->sc_flags.port_enabled = 1;
		break;
	case UHF_PORT_SUSPEND:
	case UHF_PORT_RESET:
	case UHF_PORT_TEST:
	case UHF_PORT_INDICATOR:
		/* nops */
		break;
	case UHF_PORT_POWER:
		sc->sc_flags.port_powered = 1;
		break;
	default:
		err = USB_ERR_IOERROR;
		goto done;
	}
	goto tr_valid;

tr_handle_get_port_status:

	DPRINTFN(9, "UR_GET_PORT_STATUS\n");

	if (index != 1) {
		goto tr_stalled;
	}
	if (sc->sc_flags.status_vbus) {
		atmegadci_clocks_on(sc);
		atmegadci_pull_up(sc);
	} else {
		atmegadci_pull_down(sc);
		atmegadci_clocks_off(sc);
	}

	/* Select FULL-speed and Device Side Mode */

	value = UPS_PORT_MODE_DEVICE;

	if (sc->sc_flags.port_powered) {
		value |= UPS_PORT_POWER;
	}
	if (sc->sc_flags.port_enabled) {
		value |= UPS_PORT_ENABLED;
	}
	if (sc->sc_flags.status_vbus &&
	    sc->sc_flags.status_bus_reset) {
		value |= UPS_CURRENT_CONNECT_STATUS;
	}
	if (sc->sc_flags.status_suspend) {
		value |= UPS_SUSPEND;
	}
	USETW(sc->sc_hub_temp.ps.wPortStatus, value);

	value = 0;

	if (sc->sc_flags.change_connect) {
		value |= UPS_C_CONNECT_STATUS;
	}
	if (sc->sc_flags.change_suspend) {
		value |= UPS_C_SUSPEND;
	}
	USETW(sc->sc_hub_temp.ps.wPortChange, value);
	len = sizeof(sc->sc_hub_temp.ps);
	goto tr_valid;

tr_handle_get_class_descriptor:
	if (value & 0xFF) {
		goto tr_stalled;
	}
	ptr = (const void *)&atmegadci_hubd;
	len = sizeof(atmegadci_hubd);
	goto tr_valid;

tr_stalled:
	err = USB_ERR_STALLED;
tr_valid:
done:
	*plength = len;
	*pptr = ptr;
	return (err);
}

static void
atmegadci_xfer_setup(struct usb_setup_params *parm)
{
	const struct usb_hw_ep_profile *pf;
	struct atmegadci_softc *sc;
	struct usb_xfer *xfer;
	void *last_obj;
	uint32_t ntd;
	uint32_t n;
	uint8_t ep_no;

	sc = ATMEGA_BUS2SC(parm->udev->bus);
	xfer = parm->curr_xfer;

	/*
	 * NOTE: This driver does not use any of the parameters that
	 * are computed from the following values. Just set some
	 * reasonable dummies:
	 */
	parm->hc_max_packet_size = 0x500;
	parm->hc_max_packet_count = 1;
	parm->hc_max_frame_size = 0x500;

	usbd_transfer_setup_sub(parm);

	/*
	 * compute maximum number of TDs
	 */
	if ((xfer->endpoint->edesc->bmAttributes & UE_XFERTYPE) == UE_CONTROL) {

		ntd = xfer->nframes + 1 /* STATUS */ + 1 /* SYNC 1 */
		    + 1 /* SYNC 2 */ ;
	} else {

		ntd = xfer->nframes + 1 /* SYNC */ ;
	}

	/*
	 * check if "usbd_transfer_setup_sub" set an error
	 */
	if (parm->err)
		return;

	/*
	 * allocate transfer descriptors
	 */
	last_obj = NULL;

	/*
	 * get profile stuff
	 */
	ep_no = xfer->endpointno & UE_ADDR;
	atmegadci_get_hw_ep_profile(parm->udev, &pf, ep_no);

	if (pf == NULL) {
		/* should not happen */
		parm->err = USB_ERR_INVAL;
		return;
	}

	/* align data */
	parm->size[0] += ((-parm->size[0]) & (USB_HOST_ALIGN - 1));

	for (n = 0; n != ntd; n++) {

		struct atmegadci_td *td;

		if (parm->buf) {

			td = USB_ADD_BYTES(parm->buf, parm->size[0]);

			/* init TD */
			td->max_packet_size = xfer->max_packet_size;
			td->ep_no = ep_no;
			if (pf->support_multi_buffer) {
				td->support_multi_buffer = 1;
			}
			td->obj_next = last_obj;

			last_obj = td;
		}
		parm->size[0] += sizeof(*td);
	}

	xfer->td_start[0] = last_obj;
}

static void
atmegadci_xfer_unsetup(struct usb_xfer *xfer)
{
	return;
}

static void
atmegadci_ep_init(struct usb_device *udev, struct usb_endpoint_descriptor *edesc,
    struct usb_endpoint *ep)
{
	struct atmegadci_softc *sc = ATMEGA_BUS2SC(udev->bus);

	DPRINTFN(2, "endpoint=%p, addr=%d, endpt=%d, mode=%d (%d,%d)\n",
	    ep, udev->address,
	    edesc->bEndpointAddress, udev->flags.usb_mode,
	    sc->sc_rt_addr, udev->device_index);

	if (udev->device_index != sc->sc_rt_addr) {

		if (udev->speed != USB_SPEED_FULL) {
			/* not supported */
			return;
		}
		if ((edesc->bmAttributes & UE_XFERTYPE) == UE_ISOCHRONOUS)
			ep->methods = &atmegadci_device_isoc_fs_methods;
		else
			ep->methods = &atmegadci_device_non_isoc_methods;
	}
}

static void
atmegadci_set_hw_power_sleep(struct usb_bus *bus, uint32_t state)
{
	struct atmegadci_softc *sc = ATMEGA_BUS2SC(bus);

	switch (state) {
	case USB_HW_POWER_SUSPEND:
		atmegadci_suspend(sc);
		break;
	case USB_HW_POWER_SHUTDOWN:
		atmegadci_uninit(sc);
		break;
	case USB_HW_POWER_RESUME:
		atmegadci_resume(sc);
		break;
	default:
		break;
	}
}

static const struct usb_bus_methods atmegadci_bus_methods =
{
	.endpoint_init = &atmegadci_ep_init,
	.xfer_setup = &atmegadci_xfer_setup,
	.xfer_unsetup = &atmegadci_xfer_unsetup,
	.get_hw_ep_profile = &atmegadci_get_hw_ep_profile,
	.xfer_stall = &atmegadci_xfer_stall,
	.set_stall = &atmegadci_set_stall,
	.clear_stall = &atmegadci_clear_stall,
	.roothub_exec = &atmegadci_roothub_exec,
	.xfer_poll = &atmegadci_do_poll,
	.set_hw_power_sleep = &atmegadci_set_hw_power_sleep,
};
