/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky <hselasky@FreeBSD.org>
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
 */

/*
 * This file contains the driver for the USS820 series USB Device
 * Controller
 *
 * NOTE: The datasheet does not document everything.
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

#define	USB_DEBUG_VAR uss820dcidebug

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

#include <dev/usb/controller/uss820dci.h>

#define	USS820_DCI_BUS2SC(bus) \
   ((struct uss820dci_softc *)(((uint8_t *)(bus)) - \
    ((uint8_t *)&(((struct uss820dci_softc *)0)->sc_bus))))

#define	USS820_DCI_PC2SC(pc) \
   USS820_DCI_BUS2SC(USB_DMATAG_TO_XROOT((pc)->tag_parent)->bus)

#define	USS820_DCI_THREAD_IRQ \
    (USS820_SSR_SUSPEND | USS820_SSR_RESUME | USS820_SSR_RESET)

#ifdef USB_DEBUG
static int uss820dcidebug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, uss820dci, CTLFLAG_RW, 0,
    "USB uss820dci");
SYSCTL_INT(_hw_usb_uss820dci, OID_AUTO, debug, CTLFLAG_RWTUN,
    &uss820dcidebug, 0, "uss820dci debug level");
#endif

#define	USS820_DCI_INTR_ENDPT 1

/* prototypes */

static const struct usb_bus_methods uss820dci_bus_methods;
static const struct usb_pipe_methods uss820dci_device_bulk_methods;
static const struct usb_pipe_methods uss820dci_device_ctrl_methods;
static const struct usb_pipe_methods uss820dci_device_intr_methods;
static const struct usb_pipe_methods uss820dci_device_isoc_fs_methods;

static uss820dci_cmd_t uss820dci_setup_rx;
static uss820dci_cmd_t uss820dci_data_rx;
static uss820dci_cmd_t uss820dci_data_tx;
static uss820dci_cmd_t uss820dci_data_tx_sync;
static void	uss820dci_device_done(struct usb_xfer *, usb_error_t);
static void	uss820dci_do_poll(struct usb_bus *);
static void	uss820dci_standard_done(struct usb_xfer *);
static void	uss820dci_intr_set(struct usb_xfer *, uint8_t);
static void	uss820dci_update_shared_1(struct uss820dci_softc *, uint8_t,
		    uint8_t, uint8_t);
static void	uss820dci_root_intr(struct uss820dci_softc *);

/*
 * Here is a list of what the USS820D chip can support. The main
 * limitation is that the sum of the buffer sizes must be less than
 * 1120 bytes.
 */
static const struct usb_hw_ep_profile
	uss820dci_ep_profile[] = {

	[0] = {
		.max_in_frame_size = 32,
		.max_out_frame_size = 32,
		.is_simplex = 0,
		.support_control = 1,
	},
	[1] = {
		.max_in_frame_size = 64,
		.max_out_frame_size = 64,
		.is_simplex = 0,
		.support_multi_buffer = 1,
		.support_bulk = 1,
		.support_interrupt = 1,
		.support_in = 1,
		.support_out = 1,
	},
	[2] = {
		.max_in_frame_size = 8,
		.max_out_frame_size = 8,
		.is_simplex = 0,
		.support_multi_buffer = 1,
		.support_bulk = 1,
		.support_interrupt = 1,
		.support_in = 1,
		.support_out = 1,
	},
	[3] = {
		.max_in_frame_size = 256,
		.max_out_frame_size = 256,
		.is_simplex = 0,
		.support_multi_buffer = 1,
		.support_isochronous = 1,
		.support_in = 1,
		.support_out = 1,
	},
};

static void
uss820dci_update_shared_1(struct uss820dci_softc *sc, uint8_t reg,
    uint8_t keep_mask, uint8_t set_mask)
{
	uint8_t temp;

	USS820_WRITE_1(sc, USS820_PEND, 1);
	temp = USS820_READ_1(sc, reg);
	temp &= (keep_mask);
	temp |= (set_mask);
	USS820_WRITE_1(sc, reg, temp);
	USS820_WRITE_1(sc, USS820_PEND, 0);
}

static void
uss820dci_get_hw_ep_profile(struct usb_device *udev,
    const struct usb_hw_ep_profile **ppf, uint8_t ep_addr)
{
	if (ep_addr == 0) {
		*ppf = uss820dci_ep_profile + 0;
	} else if (ep_addr < 5) {
		*ppf = uss820dci_ep_profile + 1;
	} else if (ep_addr < 7) {
		*ppf = uss820dci_ep_profile + 2;
	} else if (ep_addr == 7) {
		*ppf = uss820dci_ep_profile + 3;
	} else {
		*ppf = NULL;
	}
}

static void
uss820dci_pull_up(struct uss820dci_softc *sc)
{
	uint8_t temp;

	/* pullup D+, if possible */

	if (!sc->sc_flags.d_pulled_up &&
	    sc->sc_flags.port_powered) {
		sc->sc_flags.d_pulled_up = 1;

		DPRINTF("\n");

		temp = USS820_READ_1(sc, USS820_MCSR);
		temp |= USS820_MCSR_DPEN;
		USS820_WRITE_1(sc, USS820_MCSR, temp);
	}
}

static void
uss820dci_pull_down(struct uss820dci_softc *sc)
{
	uint8_t temp;

	/* pulldown D+, if possible */

	if (sc->sc_flags.d_pulled_up) {
		sc->sc_flags.d_pulled_up = 0;

		DPRINTF("\n");

		temp = USS820_READ_1(sc, USS820_MCSR);
		temp &= ~USS820_MCSR_DPEN;
		USS820_WRITE_1(sc, USS820_MCSR, temp);
	}
}

static void
uss820dci_wakeup_peer(struct uss820dci_softc *sc)
{
	if (!(sc->sc_flags.status_suspend)) {
		return;
	}
	DPRINTFN(0, "not supported\n");
}

static void
uss820dci_set_address(struct uss820dci_softc *sc, uint8_t addr)
{
	DPRINTFN(5, "addr=%d\n", addr);

	USS820_WRITE_1(sc, USS820_FADDR, addr);
}

static uint8_t
uss820dci_setup_rx(struct uss820dci_softc *sc, struct uss820dci_td *td)
{
	struct usb_device_request req;
	uint16_t count;
	uint8_t rx_stat;
	uint8_t temp;

	/* select the correct endpoint */
	USS820_WRITE_1(sc, USS820_EPINDEX, td->ep_index);

	/* read out FIFO status */
	rx_stat = USS820_READ_1(sc, USS820_RXSTAT);

	DPRINTFN(5, "rx_stat=0x%02x rem=%u\n", rx_stat, td->remainder);

	if (!(rx_stat & USS820_RXSTAT_RXSETUP)) {
		goto not_complete;
	}
	/* clear did stall */
	td->did_stall = 0;

	/* clear stall and all I/O */
	uss820dci_update_shared_1(sc, USS820_EPCON,
	    0xFF ^ (USS820_EPCON_TXSTL |
	    USS820_EPCON_RXSTL |
	    USS820_EPCON_RXIE |
	    USS820_EPCON_TXOE), 0);

	/* clear end overwrite flag */
	uss820dci_update_shared_1(sc, USS820_RXSTAT,
	    0xFF ^ USS820_RXSTAT_EDOVW, 0);

	/* get the packet byte count */
	count = USS820_READ_1(sc, USS820_RXCNTL);
	count |= (USS820_READ_1(sc, USS820_RXCNTH) << 8);
	count &= 0x3FF;

	/* verify data length */
	if (count != td->remainder) {
		DPRINTFN(0, "Invalid SETUP packet "
		    "length, %d bytes\n", count);
		goto setup_not_complete;
	}
	if (count != sizeof(req)) {
		DPRINTFN(0, "Unsupported SETUP packet "
		    "length, %d bytes\n", count);
		goto setup_not_complete;
	}
	/* receive data */
	bus_space_read_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
	    USS820_RXDAT * USS820_REG_STRIDE, (void *)&req, sizeof(req));

	/* read out FIFO status */
	rx_stat = USS820_READ_1(sc, USS820_RXSTAT);

	if (rx_stat & (USS820_RXSTAT_EDOVW |
	    USS820_RXSTAT_STOVW)) {
		DPRINTF("new SETUP packet received\n");
		return (1);		/* not complete */
	}
	/* clear receive setup bit */
	uss820dci_update_shared_1(sc, USS820_RXSTAT,
	    0xFF ^ (USS820_RXSTAT_RXSETUP |
	    USS820_RXSTAT_EDOVW |
	    USS820_RXSTAT_STOVW), 0);

	/* set RXFFRC bit */
	temp = USS820_READ_1(sc, USS820_RXCON);
	temp |= USS820_RXCON_RXFFRC;
	USS820_WRITE_1(sc, USS820_RXCON, temp);

	/* copy data into real buffer */
	usbd_copy_in(td->pc, 0, &req, sizeof(req));

	td->offset = sizeof(req);
	td->remainder = 0;

	/* sneak peek the set address */
	if ((req.bmRequestType == UT_WRITE_DEVICE) &&
	    (req.bRequest == UR_SET_ADDRESS)) {
		sc->sc_dv_addr = req.wValue[0] & 0x7F;
	} else {
		sc->sc_dv_addr = 0xFF;
	}

	/* reset TX FIFO */
	temp = USS820_READ_1(sc, USS820_TXCON);
	temp |= USS820_TXCON_TXCLR;
	USS820_WRITE_1(sc, USS820_TXCON, temp);
	temp &= ~USS820_TXCON_TXCLR;
	USS820_WRITE_1(sc, USS820_TXCON, temp);

	return (0);			/* complete */

setup_not_complete:

	/* set RXFFRC bit */
	temp = USS820_READ_1(sc, USS820_RXCON);
	temp |= USS820_RXCON_RXFFRC;
	USS820_WRITE_1(sc, USS820_RXCON, temp);

	/* FALLTHROUGH */

not_complete:
	/* abort any ongoing transfer */
	if (!td->did_stall) {
		DPRINTFN(5, "stalling\n");
		/* set stall */
		uss820dci_update_shared_1(sc, USS820_EPCON, 0xFF,
		    (USS820_EPCON_TXSTL | USS820_EPCON_RXSTL));

		td->did_stall = 1;
	}

	/* clear end overwrite flag, if any */
	if (rx_stat & USS820_RXSTAT_RXSETUP) {
		uss820dci_update_shared_1(sc, USS820_RXSTAT,
		    0xFF ^ (USS820_RXSTAT_EDOVW |
		    USS820_RXSTAT_STOVW |
		    USS820_RXSTAT_RXSETUP), 0);
	}
	return (1);			/* not complete */
}

static uint8_t
uss820dci_data_rx(struct uss820dci_softc *sc, struct uss820dci_td *td)
{
	struct usb_page_search buf_res;
	uint16_t count;
	uint8_t rx_flag;
	uint8_t rx_stat;
	uint8_t rx_cntl;
	uint8_t to;
	uint8_t got_short;

	to = 2;				/* don't loop forever! */
	got_short = 0;

	/* select the correct endpoint */
	USS820_WRITE_1(sc, USS820_EPINDEX, td->ep_index);

	/* check if any of the FIFO banks have data */
repeat:
	/* read out FIFO flag */
	rx_flag = USS820_READ_1(sc, USS820_RXFLG);
	/* read out FIFO status */
	rx_stat = USS820_READ_1(sc, USS820_RXSTAT);

	DPRINTFN(5, "rx_stat=0x%02x rx_flag=0x%02x rem=%u\n",
	    rx_stat, rx_flag, td->remainder);

	if (rx_stat & (USS820_RXSTAT_RXSETUP |
	    USS820_RXSTAT_RXSOVW |
	    USS820_RXSTAT_EDOVW)) {
		if (td->remainder == 0 && td->ep_index == 0) {
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
	/* check for errors */
	if (rx_flag & (USS820_RXFLG_RXOVF |
	    USS820_RXFLG_RXURF)) {
		DPRINTFN(5, "overflow or underflow\n");
		/* should not happen */
		td->error = 1;
		return (0);		/* complete */
	}
	/* check status */
	if (!(rx_flag & (USS820_RXFLG_RXFIF0 |
	    USS820_RXFLG_RXFIF1))) {

		/* read out EPCON register */
		/* enable RX input */
		if (!td->did_enable) {
			uss820dci_update_shared_1(USS820_DCI_PC2SC(td->pc),
			    USS820_EPCON, 0xFF, USS820_EPCON_RXIE);
			td->did_enable = 1;
		}
		return (1);		/* not complete */
	}
	/* get the packet byte count */
	count = USS820_READ_1(sc, USS820_RXCNTL);
	count |= (USS820_READ_1(sc, USS820_RXCNTH) << 8);
	count &= 0x3FF;

	DPRINTFN(5, "count=0x%04x\n", count);

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
		bus_space_read_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
		    USS820_RXDAT * USS820_REG_STRIDE, buf_res.buffer, buf_res.length);

		/* update counters */
		count -= buf_res.length;
		td->offset += buf_res.length;
		td->remainder -= buf_res.length;
	}

	/* set RXFFRC bit */
	rx_cntl = USS820_READ_1(sc, USS820_RXCON);
	rx_cntl |= USS820_RXCON_RXFFRC;
	USS820_WRITE_1(sc, USS820_RXCON, rx_cntl);

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
	return (1);			/* not complete */
}

static uint8_t
uss820dci_data_tx(struct uss820dci_softc *sc, struct uss820dci_td *td)
{
	struct usb_page_search buf_res;
	uint16_t count;
	uint16_t count_copy;
	uint8_t rx_stat;
	uint8_t tx_flag;
	uint8_t to;

	/* select the correct endpoint */
	USS820_WRITE_1(sc, USS820_EPINDEX, td->ep_index);

	to = 2;				/* don't loop forever! */

repeat:
	/* read out TX FIFO flags */
	tx_flag = USS820_READ_1(sc, USS820_TXFLG);

	DPRINTFN(5, "tx_flag=0x%02x rem=%u\n", tx_flag, td->remainder);

	if (td->ep_index == 0) {
		/* read out RX FIFO status last */
		rx_stat = USS820_READ_1(sc, USS820_RXSTAT);

		DPRINTFN(5, "rx_stat=0x%02x\n", rx_stat);

		if (rx_stat & (USS820_RXSTAT_RXSETUP |
		    USS820_RXSTAT_RXSOVW |
		    USS820_RXSTAT_EDOVW)) {
			/*
			 * The current transfer was aborted by the USB
			 * Host:
			 */
			td->error = 1;
			return (0);		/* complete */
		}
	}
	if (tx_flag & (USS820_TXFLG_TXOVF |
	    USS820_TXFLG_TXURF)) {
		td->error = 1;
		return (0);		/* complete */
	}
	if (tx_flag & USS820_TXFLG_TXFIF0) {
		if (tx_flag & USS820_TXFLG_TXFIF1) {
			return (1);	/* not complete */
		}
	}
	if ((!td->support_multi_buffer) &&
	    (tx_flag & (USS820_TXFLG_TXFIF0 |
	    USS820_TXFLG_TXFIF1))) {
		return (1);		/* not complete */
	}
	count = td->max_packet_size;
	if (td->remainder < count) {
		/* we have a short packet */
		td->short_pkt = 1;
		count = td->remainder;
	}
	count_copy = count;
	while (count > 0) {

		usbd_get_page(td->pc, td->offset, &buf_res);

		/* get correct length */
		if (buf_res.length > count) {
			buf_res.length = count;
		}
		/* transmit data */
		bus_space_write_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
		    USS820_TXDAT * USS820_REG_STRIDE, buf_res.buffer, buf_res.length);

		/* update counters */
		count -= buf_res.length;
		td->offset += buf_res.length;
		td->remainder -= buf_res.length;
	}

	/* post-write high packet byte count first */
	USS820_WRITE_1(sc, USS820_TXCNTH, count_copy >> 8);

	/* post-write low packet byte count last */
	USS820_WRITE_1(sc, USS820_TXCNTL, count_copy);

	/*
	 * Enable TX output, which must happen after that we have written
	 * data into the FIFO. This is undocumented.
	 */
	if (!td->did_enable) {
		uss820dci_update_shared_1(USS820_DCI_PC2SC(td->pc),
		    USS820_EPCON, 0xFF, USS820_EPCON_TXOE);
		td->did_enable = 1;
	}
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
	return (1);			/* not complete */
}

static uint8_t
uss820dci_data_tx_sync(struct uss820dci_softc *sc, struct uss820dci_td *td)
{
	uint8_t rx_stat;
	uint8_t tx_flag;

	/* select the correct endpoint */
	USS820_WRITE_1(sc, USS820_EPINDEX, td->ep_index);

	/* read out TX FIFO flag */
	tx_flag = USS820_READ_1(sc, USS820_TXFLG);

	if (td->ep_index == 0) {
		/* read out RX FIFO status last */
		rx_stat = USS820_READ_1(sc, USS820_RXSTAT);

		DPRINTFN(5, "rx_stat=0x%02x rem=%u\n", rx_stat, td->remainder);

		if (rx_stat & (USS820_RXSTAT_RXSETUP |
		    USS820_RXSTAT_RXSOVW |
		    USS820_RXSTAT_EDOVW)) {
			DPRINTFN(5, "faking complete\n");
			/* Race condition */
			return (0);		/* complete */
		}
	}
	DPRINTFN(5, "tx_flag=0x%02x rem=%u\n", tx_flag, td->remainder);

	if (tx_flag & (USS820_TXFLG_TXOVF |
	    USS820_TXFLG_TXURF)) {
		td->error = 1;
		return (0);		/* complete */
	}
	if (tx_flag & (USS820_TXFLG_TXFIF0 |
	    USS820_TXFLG_TXFIF1)) {
		return (1);		/* not complete */
	}
	if (td->ep_index == 0 && sc->sc_dv_addr != 0xFF) {
		/* write function address */
		uss820dci_set_address(sc, sc->sc_dv_addr);
	}
	return (0);			/* complete */
}

static void
uss820dci_xfer_do_fifo(struct usb_xfer *xfer)
{
	struct uss820dci_softc *sc = USS820_DCI_BUS2SC(xfer->xroot->bus);
	struct uss820dci_td *td;

	DPRINTFN(9, "\n");

	td = xfer->td_transfer_cache;
	if (td == NULL)
		return;

	while (1) {
		if ((td->func) (sc, td)) {
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
		 * Fetch the next transfer descriptor.
		 */
		td = td->obj_next;
		xfer->td_transfer_cache = td;
	}
	return;

done:
	/* compute all actual lengths */
	xfer->td_transfer_cache = NULL;
	sc->sc_xfer_complete = 1;
}

static uint8_t
uss820dci_xfer_do_complete(struct usb_xfer *xfer)
{
	struct uss820dci_td *td;

	DPRINTFN(9, "\n");

	td = xfer->td_transfer_cache;
	if (td == NULL) {
		/* compute all actual lengths */
		uss820dci_standard_done(xfer);
		return(1);
	}
	return (0);
}

static void
uss820dci_interrupt_poll_locked(struct uss820dci_softc *sc)
{
	struct usb_xfer *xfer;

	TAILQ_FOREACH(xfer, &sc->sc_bus.intr_q.head, wait_entry)
		uss820dci_xfer_do_fifo(xfer);
}

static void
uss820dci_interrupt_complete_locked(struct uss820dci_softc *sc)
{
	struct usb_xfer *xfer;
repeat:
	TAILQ_FOREACH(xfer, &sc->sc_bus.intr_q.head, wait_entry) {
		if (uss820dci_xfer_do_complete(xfer))
			goto repeat;
	}
}

static void
uss820dci_wait_suspend(struct uss820dci_softc *sc, uint8_t on)
{
	uint8_t scr;
	uint8_t scratch;

	scr = USS820_READ_1(sc, USS820_SCR);
	scratch = USS820_READ_1(sc, USS820_SCRATCH);

	if (on) {
		scr |= USS820_SCR_IE_SUSP;
		scratch &= ~USS820_SCRATCH_IE_RESUME;
	} else {
		scr &= ~USS820_SCR_IE_SUSP;
		scratch |= USS820_SCRATCH_IE_RESUME;
	}

	USS820_WRITE_1(sc, USS820_SCR, scr);
	USS820_WRITE_1(sc, USS820_SCRATCH, scratch);
}

int
uss820dci_filter_interrupt(void *arg)
{
	struct uss820dci_softc *sc = arg;
	int retval = FILTER_HANDLED;
	uint8_t ssr;

	USB_BUS_SPIN_LOCK(&sc->sc_bus);

	ssr = USS820_READ_1(sc, USS820_SSR);
	uss820dci_update_shared_1(sc, USS820_SSR, USS820_DCI_THREAD_IRQ, 0);

	if (ssr & USS820_DCI_THREAD_IRQ)
		retval = FILTER_SCHEDULE_THREAD;

	/* poll FIFOs, if any */
	uss820dci_interrupt_poll_locked(sc);

	if (sc->sc_xfer_complete != 0)
		retval = FILTER_SCHEDULE_THREAD;

	USB_BUS_SPIN_UNLOCK(&sc->sc_bus);

	return (retval);
}

void
uss820dci_interrupt(void *arg)
{
	struct uss820dci_softc *sc = arg;
	uint8_t ssr;
	uint8_t event;

	USB_BUS_LOCK(&sc->sc_bus);
	USB_BUS_SPIN_LOCK(&sc->sc_bus);

	ssr = USS820_READ_1(sc, USS820_SSR);

	/* acknowledge all interrupts */

	uss820dci_update_shared_1(sc, USS820_SSR, ~USS820_DCI_THREAD_IRQ, 0);

	/* check for any bus state change interrupts */

	if (ssr & USS820_DCI_THREAD_IRQ) {

		event = 0;

		if (ssr & USS820_SSR_RESET) {
			sc->sc_flags.status_bus_reset = 1;
			sc->sc_flags.status_suspend = 0;
			sc->sc_flags.change_suspend = 0;
			sc->sc_flags.change_connect = 1;

			/* disable resume interrupt */
			uss820dci_wait_suspend(sc, 1);

			event = 1;
		}
		/*
	         * If "RESUME" and "SUSPEND" is set at the same time
	         * we interpret that like "RESUME". Resume is set when
	         * there is at least 3 milliseconds of inactivity on
	         * the USB BUS.
	         */
		if (ssr & USS820_SSR_RESUME) {
			if (sc->sc_flags.status_suspend) {
				sc->sc_flags.status_suspend = 0;
				sc->sc_flags.change_suspend = 1;
				/* disable resume interrupt */
				uss820dci_wait_suspend(sc, 1);
				event = 1;
			}
		} else if (ssr & USS820_SSR_SUSPEND) {
			if (!sc->sc_flags.status_suspend) {
				sc->sc_flags.status_suspend = 1;
				sc->sc_flags.change_suspend = 1;
				/* enable resume interrupt */
				uss820dci_wait_suspend(sc, 0);
				event = 1;
			}
		}
		if (event) {

			DPRINTF("real bus interrupt 0x%02x\n", ssr);

			/* complete root HUB interrupt endpoint */
			uss820dci_root_intr(sc);
		}
	}
	/* acknowledge all SBI interrupts */
	uss820dci_update_shared_1(sc, USS820_SBI, 0, 0);

	/* acknowledge all SBI1 interrupts */
	uss820dci_update_shared_1(sc, USS820_SBI1, 0, 0);

	if (sc->sc_xfer_complete != 0) {
		sc->sc_xfer_complete = 0;

		/* complete FIFOs, if any */
		uss820dci_interrupt_complete_locked(sc);
	}
	USB_BUS_SPIN_UNLOCK(&sc->sc_bus);
	USB_BUS_UNLOCK(&sc->sc_bus);
}

static void
uss820dci_setup_standard_chain_sub(struct uss820_std_temp *temp)
{
	struct uss820dci_td *td;

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
	td->did_enable = 0;
	td->did_stall = temp->did_stall;
	td->short_pkt = temp->short_pkt;
	td->alt_next = temp->setup_alt_next;
}

static void
uss820dci_setup_standard_chain(struct usb_xfer *xfer)
{
	struct uss820_std_temp temp;
	struct uss820dci_softc *sc;
	struct uss820dci_td *td;
	uint32_t x;
	uint8_t ep_no;

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

	sc = USS820_DCI_BUS2SC(xfer->xroot->bus);
	ep_no = (xfer->endpointno & UE_ADDR);

	/* check if we should prepend a setup message */

	if (xfer->flags_int.control_xfr) {
		if (xfer->flags_int.control_hdr) {

			temp.func = &uss820dci_setup_rx;
			temp.len = xfer->frlengths[0];
			temp.pc = xfer->frbuffers + 0;
			temp.short_pkt = temp.len ? 1 : 0;
			/* check for last frame */
			if (xfer->nframes == 1) {
				/* no STATUS stage yet, SETUP is last */
				if (xfer->flags_int.control_act)
					temp.setup_alt_next = 0;
			}

			uss820dci_setup_standard_chain_sub(&temp);
		}
		x = 1;
	} else {
		x = 0;
	}

	if (x != xfer->nframes) {
		if (xfer->endpointno & UE_DIR_IN) {
			temp.func = &uss820dci_data_tx;
		} else {
			temp.func = &uss820dci_data_rx;
		}

		/* setup "pc" pointer */
		temp.pc = xfer->frbuffers + x;
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

		uss820dci_setup_standard_chain_sub(&temp);

		if (xfer->flags_int.isochronous_xfr) {
			temp.offset += temp.len;
		} else {
			/* get next Page Cache pointer */
			temp.pc = xfer->frbuffers + x;
		}
	}

	/* check for control transfer */
	if (xfer->flags_int.control_xfr) {
		uint8_t need_sync;

		/* always setup a valid "pc" pointer for status and sync */
		temp.pc = xfer->frbuffers + 0;
		temp.len = 0;
		temp.short_pkt = 0;
		temp.setup_alt_next = 0;

		/* check if we should append a status stage */
		if (!xfer->flags_int.control_act) {

			/*
			 * Send a DATA1 message and invert the current
			 * endpoint direction.
			 */
			if (xfer->endpointno & UE_DIR_IN) {
				temp.func = &uss820dci_data_rx;
				need_sync = 0;
			} else {
				temp.func = &uss820dci_data_tx;
				need_sync = 1;
			}
			temp.len = 0;
			temp.short_pkt = 0;

			uss820dci_setup_standard_chain_sub(&temp);
			if (need_sync) {
				/* we need a SYNC point after TX */
				temp.func = &uss820dci_data_tx_sync;
				uss820dci_setup_standard_chain_sub(&temp);
			}
		}
	}
	/* must have at least one frame! */
	td = temp.td;
	xfer->td_transfer_last = td;
}

static void
uss820dci_timeout(void *arg)
{
	struct usb_xfer *xfer = arg;

	DPRINTF("xfer=%p\n", xfer);

	USB_BUS_LOCK_ASSERT(xfer->xroot->bus, MA_OWNED);

	/* transfer is transferred */
	uss820dci_device_done(xfer, USB_ERR_TIMEOUT);
}

static void
uss820dci_intr_set(struct usb_xfer *xfer, uint8_t set)
{
	struct uss820dci_softc *sc = USS820_DCI_BUS2SC(xfer->xroot->bus);
	uint8_t ep_no = (xfer->endpointno & UE_ADDR);
	uint8_t ep_reg;
	uint8_t temp;

	DPRINTFN(15, "endpoint 0x%02x\n", xfer->endpointno);

	if (ep_no > 3) {
		ep_reg = USS820_SBIE1;
	} else {
		ep_reg = USS820_SBIE;
	}

	ep_no &= 3;
	ep_no = 1 << (2 * ep_no);

	if (xfer->flags_int.control_xfr) {
		if (xfer->flags_int.control_hdr) {
			ep_no <<= 1;	/* RX interrupt only */
		} else {
			ep_no |= (ep_no << 1);	/* RX and TX interrupt */
		}
	} else {
		if (!(xfer->endpointno & UE_DIR_IN)) {
			ep_no <<= 1;
		}
	}
	temp = USS820_READ_1(sc, ep_reg);
	if (set) {
		temp |= ep_no;
	} else {
		temp &= ~ep_no;
	}
	USS820_WRITE_1(sc, ep_reg, temp);
}

static void
uss820dci_start_standard_chain(struct usb_xfer *xfer)
{
	struct uss820dci_softc *sc = USS820_DCI_BUS2SC(xfer->xroot->bus);

	DPRINTFN(9, "\n");

	USB_BUS_SPIN_LOCK(&sc->sc_bus);

	/* poll one time */
	uss820dci_xfer_do_fifo(xfer);

	if (uss820dci_xfer_do_complete(xfer) == 0) {
		/*
		 * Only enable the endpoint interrupt when we are
		 * actually waiting for data, hence we are dealing
		 * with level triggered interrupts !
		 */
		uss820dci_intr_set(xfer, 1);

		/* put transfer on interrupt queue */
		usbd_transfer_enqueue(&xfer->xroot->bus->intr_q, xfer);

		/* start timeout, if any */
		if (xfer->timeout != 0) {
			usbd_transfer_timeout_ms(xfer,
			    &uss820dci_timeout, xfer->timeout);
		}
	}
	USB_BUS_SPIN_UNLOCK(&sc->sc_bus);
}

static void
uss820dci_root_intr(struct uss820dci_softc *sc)
{
	DPRINTFN(9, "\n");

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	/* set port bit */
	sc->sc_hub_idata[0] = 0x02;	/* we only have one port */

	uhub_root_intr(&sc->sc_bus, sc->sc_hub_idata,
	    sizeof(sc->sc_hub_idata));
}

static usb_error_t
uss820dci_standard_done_sub(struct usb_xfer *xfer)
{
	struct uss820dci_td *td;
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
uss820dci_standard_done(struct usb_xfer *xfer)
{
	usb_error_t err = 0;

	DPRINTFN(13, "xfer=%p endpoint=%p transfer done\n",
	    xfer, xfer->endpoint);

	/* reset scanner */

	xfer->td_transfer_cache = xfer->td_transfer_first;

	if (xfer->flags_int.control_xfr) {

		if (xfer->flags_int.control_hdr) {

			err = uss820dci_standard_done_sub(xfer);
		}
		xfer->aframes = 1;

		if (xfer->td_transfer_cache == NULL) {
			goto done;
		}
	}
	while (xfer->aframes != xfer->nframes) {

		err = uss820dci_standard_done_sub(xfer);
		xfer->aframes++;

		if (xfer->td_transfer_cache == NULL) {
			goto done;
		}
	}

	if (xfer->flags_int.control_xfr &&
	    !xfer->flags_int.control_act) {

		err = uss820dci_standard_done_sub(xfer);
	}
done:
	uss820dci_device_done(xfer, err);
}

/*------------------------------------------------------------------------*
 *	uss820dci_device_done
 *
 * NOTE: this function can be called more than one time on the
 * same USB transfer!
 *------------------------------------------------------------------------*/
static void
uss820dci_device_done(struct usb_xfer *xfer, usb_error_t error)
{
	struct uss820dci_softc *sc = USS820_DCI_BUS2SC(xfer->xroot->bus);

	USB_BUS_LOCK_ASSERT(xfer->xroot->bus, MA_OWNED);

	DPRINTFN(2, "xfer=%p, endpoint=%p, error=%d\n",
	    xfer, xfer->endpoint, error);

	USB_BUS_SPIN_LOCK(&sc->sc_bus);

	if (xfer->flags_int.usb_mode == USB_MODE_DEVICE) {
		uss820dci_intr_set(xfer, 0);
	}
	/* dequeue transfer and start next transfer */
	usbd_transfer_done(xfer, error);

	USB_BUS_SPIN_UNLOCK(&sc->sc_bus);
}

static void
uss820dci_xfer_stall(struct usb_xfer *xfer)
{
	uss820dci_device_done(xfer, USB_ERR_STALLED);
}

static void
uss820dci_set_stall(struct usb_device *udev,
    struct usb_endpoint *ep, uint8_t *did_stall)
{
	struct uss820dci_softc *sc;
	uint8_t ep_no;
	uint8_t ep_type;
	uint8_t ep_dir;
	uint8_t temp;

	USB_BUS_LOCK_ASSERT(udev->bus, MA_OWNED);

	DPRINTFN(5, "endpoint=%p\n", ep);

	/* set FORCESTALL */
	sc = USS820_DCI_BUS2SC(udev->bus);
	ep_no = (ep->edesc->bEndpointAddress & UE_ADDR);
	ep_dir = (ep->edesc->bEndpointAddress & (UE_DIR_IN | UE_DIR_OUT));
	ep_type = (ep->edesc->bmAttributes & UE_XFERTYPE);

	if (ep_type == UE_CONTROL) {
		/* should not happen */
		return;
	}
	USB_BUS_SPIN_LOCK(&sc->sc_bus);
	USS820_WRITE_1(sc, USS820_EPINDEX, ep_no);

	if (ep_dir == UE_DIR_IN) {
		temp = USS820_EPCON_TXSTL;
	} else {
		temp = USS820_EPCON_RXSTL;
	}
	uss820dci_update_shared_1(sc, USS820_EPCON, 0xFF, temp);
	USB_BUS_SPIN_UNLOCK(&sc->sc_bus);
}

static void
uss820dci_clear_stall_sub(struct uss820dci_softc *sc,
    uint8_t ep_no, uint8_t ep_type, uint8_t ep_dir)
{
	uint8_t temp;

	if (ep_type == UE_CONTROL) {
		/* clearing stall is not needed */
		return;
	}
	USB_BUS_SPIN_LOCK(&sc->sc_bus);

	/* select endpoint index */
	USS820_WRITE_1(sc, USS820_EPINDEX, ep_no);

	/* clear stall and disable I/O transfers */
	if (ep_dir == UE_DIR_IN) {
		temp = 0xFF ^ (USS820_EPCON_TXOE |
		    USS820_EPCON_TXSTL);
	} else {
		temp = 0xFF ^ (USS820_EPCON_RXIE |
		    USS820_EPCON_RXSTL);
	}
	uss820dci_update_shared_1(sc, USS820_EPCON, temp, 0);

	if (ep_dir == UE_DIR_IN) {
		/* reset data toggle */
		USS820_WRITE_1(sc, USS820_TXSTAT,
		    USS820_TXSTAT_TXSOVW);

		/* reset FIFO */
		temp = USS820_READ_1(sc, USS820_TXCON);
		temp |= USS820_TXCON_TXCLR;
		USS820_WRITE_1(sc, USS820_TXCON, temp);
		temp &= ~USS820_TXCON_TXCLR;
		USS820_WRITE_1(sc, USS820_TXCON, temp);
	} else {

		/* reset data toggle */
		uss820dci_update_shared_1(sc, USS820_RXSTAT,
		    0, USS820_RXSTAT_RXSOVW);

		/* reset FIFO */
		temp = USS820_READ_1(sc, USS820_RXCON);
		temp |= USS820_RXCON_RXCLR;
		temp &= ~USS820_RXCON_RXFFRC;
		USS820_WRITE_1(sc, USS820_RXCON, temp);
		temp &= ~USS820_RXCON_RXCLR;
		USS820_WRITE_1(sc, USS820_RXCON, temp);
	}
	USB_BUS_SPIN_UNLOCK(&sc->sc_bus);
}

static void
uss820dci_clear_stall(struct usb_device *udev, struct usb_endpoint *ep)
{
	struct uss820dci_softc *sc;
	struct usb_endpoint_descriptor *ed;

	USB_BUS_LOCK_ASSERT(udev->bus, MA_OWNED);

	DPRINTFN(5, "endpoint=%p\n", ep);

	/* check mode */
	if (udev->flags.usb_mode != USB_MODE_DEVICE) {
		/* not supported */
		return;
	}
	/* get softc */
	sc = USS820_DCI_BUS2SC(udev->bus);

	/* get endpoint descriptor */
	ed = ep->edesc;

	/* reset endpoint */
	uss820dci_clear_stall_sub(sc,
	    (ed->bEndpointAddress & UE_ADDR),
	    (ed->bmAttributes & UE_XFERTYPE),
	    (ed->bEndpointAddress & (UE_DIR_IN | UE_DIR_OUT)));
}

usb_error_t
uss820dci_init(struct uss820dci_softc *sc)
{
	const struct usb_hw_ep_profile *pf;
	uint8_t n;
	uint8_t temp;

	DPRINTF("start\n");

	/* set up the bus structure */
	sc->sc_bus.usbrev = USB_REV_1_1;
	sc->sc_bus.methods = &uss820dci_bus_methods;

	USB_BUS_LOCK(&sc->sc_bus);

	/* we always have VBUS */
	sc->sc_flags.status_vbus = 1;

	/* reset the chip */
	USS820_WRITE_1(sc, USS820_SCR, USS820_SCR_SRESET);
	DELAY(100);
	USS820_WRITE_1(sc, USS820_SCR, 0);

	/* wait for reset to complete */
	for (n = 0;; n++) {

		temp = USS820_READ_1(sc, USS820_MCSR);

		if (temp & USS820_MCSR_INIT) {
			break;
		}
		if (n == 100) {
			USB_BUS_UNLOCK(&sc->sc_bus);
			return (USB_ERR_INVAL);
		}
		/* wait a little for things to stabilise */
		DELAY(100);
	}

	/* do a pulldown */
	uss820dci_pull_down(sc);

	/* wait 10ms for pulldown to stabilise */
	usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 100);

	/* check hardware revision */
	temp = USS820_READ_1(sc, USS820_REV);

	if (temp < 0x13) {
		USB_BUS_UNLOCK(&sc->sc_bus);
		return (USB_ERR_INVAL);
	}
	/* enable interrupts */
	USS820_WRITE_1(sc, USS820_SCR,
	    USS820_SCR_T_IRQ |
	    USS820_SCR_IE_RESET |
	/* USS820_SCR_RWUPE | */
	    USS820_SCR_IE_SUSP |
	    USS820_SCR_IRQPOL);

	/* enable interrupts */
	USS820_WRITE_1(sc, USS820_SCRATCH,
	    USS820_SCRATCH_IE_RESUME);

	/* enable features */
	USS820_WRITE_1(sc, USS820_MCSR,
	    USS820_MCSR_BDFEAT |
	    USS820_MCSR_FEAT);

	sc->sc_flags.mcsr_feat = 1;

	/* disable interrupts */
	USS820_WRITE_1(sc, USS820_SBIE, 0);

	/* disable interrupts */
	USS820_WRITE_1(sc, USS820_SBIE1, 0);

	/* disable all endpoints */
	for (n = 0; n != USS820_EP_MAX; n++) {

		/* select endpoint */
		USS820_WRITE_1(sc, USS820_EPINDEX, n);

		/* disable endpoint */
		uss820dci_update_shared_1(sc, USS820_EPCON, 0, 0);
	}

	/*
	 * Initialise default values for some registers that cannot be
	 * changed during operation!
	 */
	for (n = 0; n != USS820_EP_MAX; n++) {

		uss820dci_get_hw_ep_profile(NULL, &pf, n);

		/* the maximum frame sizes should be the same */
		if (pf->max_in_frame_size != pf->max_out_frame_size) {
			DPRINTF("Max frame size mismatch %u != %u\n",
			    pf->max_in_frame_size, pf->max_out_frame_size);
		}
		if (pf->support_isochronous) {
			if (pf->max_in_frame_size <= 64) {
				temp = (USS820_TXCON_FFSZ_16_64 |
				    USS820_TXCON_TXISO |
				    USS820_TXCON_ATM);
			} else if (pf->max_in_frame_size <= 256) {
				temp = (USS820_TXCON_FFSZ_64_256 |
				    USS820_TXCON_TXISO |
				    USS820_TXCON_ATM);
			} else if (pf->max_in_frame_size <= 512) {
				temp = (USS820_TXCON_FFSZ_8_512 |
				    USS820_TXCON_TXISO |
				    USS820_TXCON_ATM);
			} else {	/* 1024 bytes */
				temp = (USS820_TXCON_FFSZ_32_1024 |
				    USS820_TXCON_TXISO |
				    USS820_TXCON_ATM);
			}
		} else {
			if ((pf->max_in_frame_size <= 8) &&
			    (sc->sc_flags.mcsr_feat)) {
				temp = (USS820_TXCON_FFSZ_8_512 |
				    USS820_TXCON_ATM);
			} else if (pf->max_in_frame_size <= 16) {
				temp = (USS820_TXCON_FFSZ_16_64 |
				    USS820_TXCON_ATM);
			} else if ((pf->max_in_frame_size <= 32) &&
			    (sc->sc_flags.mcsr_feat)) {
				temp = (USS820_TXCON_FFSZ_32_1024 |
				    USS820_TXCON_ATM);
			} else {	/* 64 bytes */
				temp = (USS820_TXCON_FFSZ_64_256 |
				    USS820_TXCON_ATM);
			}
		}

		/* need to configure the chip early */

		USS820_WRITE_1(sc, USS820_EPINDEX, n);
		USS820_WRITE_1(sc, USS820_TXCON, temp);
		USS820_WRITE_1(sc, USS820_RXCON, temp);

		if (pf->support_control) {
			temp = USS820_EPCON_CTLEP |
			    USS820_EPCON_RXSPM |
			    USS820_EPCON_RXIE |
			    USS820_EPCON_RXEPEN |
			    USS820_EPCON_TXOE |
			    USS820_EPCON_TXEPEN;
		} else {
			temp = USS820_EPCON_RXEPEN | USS820_EPCON_TXEPEN;
		}

		uss820dci_update_shared_1(sc, USS820_EPCON, 0, temp);
	}

	USB_BUS_UNLOCK(&sc->sc_bus);

	/* catch any lost interrupts */

	uss820dci_do_poll(&sc->sc_bus);

	return (0);			/* success */
}

void
uss820dci_uninit(struct uss820dci_softc *sc)
{
	uint8_t temp;

	USB_BUS_LOCK(&sc->sc_bus);

	/* disable all interrupts */
	temp = USS820_READ_1(sc, USS820_SCR);
	temp &= ~USS820_SCR_T_IRQ;
	USS820_WRITE_1(sc, USS820_SCR, temp);

	sc->sc_flags.port_powered = 0;
	sc->sc_flags.status_vbus = 0;
	sc->sc_flags.status_bus_reset = 0;
	sc->sc_flags.status_suspend = 0;
	sc->sc_flags.change_suspend = 0;
	sc->sc_flags.change_connect = 1;

	uss820dci_pull_down(sc);
	USB_BUS_UNLOCK(&sc->sc_bus);
}

static void
uss820dci_suspend(struct uss820dci_softc *sc)
{
	/* TODO */
}

static void
uss820dci_resume(struct uss820dci_softc *sc)
{
	/* TODO */
}

static void
uss820dci_do_poll(struct usb_bus *bus)
{
	struct uss820dci_softc *sc = USS820_DCI_BUS2SC(bus);

	USB_BUS_LOCK(&sc->sc_bus);
	USB_BUS_SPIN_LOCK(&sc->sc_bus);
	uss820dci_interrupt_poll_locked(sc);
	uss820dci_interrupt_complete_locked(sc);
	USB_BUS_SPIN_UNLOCK(&sc->sc_bus);
	USB_BUS_UNLOCK(&sc->sc_bus);
}

/*------------------------------------------------------------------------*
 * uss820dci bulk support
 *------------------------------------------------------------------------*/
static void
uss820dci_device_bulk_open(struct usb_xfer *xfer)
{
	return;
}

static void
uss820dci_device_bulk_close(struct usb_xfer *xfer)
{
	uss820dci_device_done(xfer, USB_ERR_CANCELLED);
}

static void
uss820dci_device_bulk_enter(struct usb_xfer *xfer)
{
	return;
}

static void
uss820dci_device_bulk_start(struct usb_xfer *xfer)
{
	/* setup TDs */
	uss820dci_setup_standard_chain(xfer);
	uss820dci_start_standard_chain(xfer);
}

static const struct usb_pipe_methods uss820dci_device_bulk_methods =
{
	.open = uss820dci_device_bulk_open,
	.close = uss820dci_device_bulk_close,
	.enter = uss820dci_device_bulk_enter,
	.start = uss820dci_device_bulk_start,
};

/*------------------------------------------------------------------------*
 * uss820dci control support
 *------------------------------------------------------------------------*/
static void
uss820dci_device_ctrl_open(struct usb_xfer *xfer)
{
	return;
}

static void
uss820dci_device_ctrl_close(struct usb_xfer *xfer)
{
	uss820dci_device_done(xfer, USB_ERR_CANCELLED);
}

static void
uss820dci_device_ctrl_enter(struct usb_xfer *xfer)
{
	return;
}

static void
uss820dci_device_ctrl_start(struct usb_xfer *xfer)
{
	/* setup TDs */
	uss820dci_setup_standard_chain(xfer);
	uss820dci_start_standard_chain(xfer);
}

static const struct usb_pipe_methods uss820dci_device_ctrl_methods =
{
	.open = uss820dci_device_ctrl_open,
	.close = uss820dci_device_ctrl_close,
	.enter = uss820dci_device_ctrl_enter,
	.start = uss820dci_device_ctrl_start,
};

/*------------------------------------------------------------------------*
 * uss820dci interrupt support
 *------------------------------------------------------------------------*/
static void
uss820dci_device_intr_open(struct usb_xfer *xfer)
{
	return;
}

static void
uss820dci_device_intr_close(struct usb_xfer *xfer)
{
	uss820dci_device_done(xfer, USB_ERR_CANCELLED);
}

static void
uss820dci_device_intr_enter(struct usb_xfer *xfer)
{
	return;
}

static void
uss820dci_device_intr_start(struct usb_xfer *xfer)
{
	/* setup TDs */
	uss820dci_setup_standard_chain(xfer);
	uss820dci_start_standard_chain(xfer);
}

static const struct usb_pipe_methods uss820dci_device_intr_methods =
{
	.open = uss820dci_device_intr_open,
	.close = uss820dci_device_intr_close,
	.enter = uss820dci_device_intr_enter,
	.start = uss820dci_device_intr_start,
};

/*------------------------------------------------------------------------*
 * uss820dci full speed isochronous support
 *------------------------------------------------------------------------*/
static void
uss820dci_device_isoc_fs_open(struct usb_xfer *xfer)
{
	return;
}

static void
uss820dci_device_isoc_fs_close(struct usb_xfer *xfer)
{
	uss820dci_device_done(xfer, USB_ERR_CANCELLED);
}

static void
uss820dci_device_isoc_fs_enter(struct usb_xfer *xfer)
{
	struct uss820dci_softc *sc = USS820_DCI_BUS2SC(xfer->xroot->bus);
	uint32_t temp;
	uint32_t nframes;

	DPRINTFN(6, "xfer=%p next=%d nframes=%d\n",
	    xfer, xfer->endpoint->isoc_next, xfer->nframes);

	/* get the current frame index - we don't need the high bits */

	nframes = USS820_READ_1(sc, USS820_SOFL);

	/*
	 * check if the frame index is within the window where the
	 * frames will be inserted
	 */
	temp = (nframes - xfer->endpoint->isoc_next) & USS820_SOFL_MASK;

	if ((xfer->endpoint->is_synced == 0) ||
	    (temp < xfer->nframes)) {
		/*
		 * If there is data underflow or the pipe queue is
		 * empty we schedule the transfer a few frames ahead
		 * of the current frame position. Else two isochronous
		 * transfers might overlap.
		 */
		xfer->endpoint->isoc_next = (nframes + 3) & USS820_SOFL_MASK;
		xfer->endpoint->is_synced = 1;
		DPRINTFN(3, "start next=%d\n", xfer->endpoint->isoc_next);
	}
	/*
	 * compute how many milliseconds the insertion is ahead of the
	 * current frame position:
	 */
	temp = (xfer->endpoint->isoc_next - nframes) & USS820_SOFL_MASK;

	/*
	 * pre-compute when the isochronous transfer will be finished:
	 */
	xfer->isoc_time_complete =
	    usb_isoc_time_expand(&sc->sc_bus, nframes) + temp +
	    xfer->nframes;

	/* compute frame number for next insertion */
	xfer->endpoint->isoc_next += xfer->nframes;

	/* setup TDs */
	uss820dci_setup_standard_chain(xfer);
}

static void
uss820dci_device_isoc_fs_start(struct usb_xfer *xfer)
{
	/* start TD chain */
	uss820dci_start_standard_chain(xfer);
}

static const struct usb_pipe_methods uss820dci_device_isoc_fs_methods =
{
	.open = uss820dci_device_isoc_fs_open,
	.close = uss820dci_device_isoc_fs_close,
	.enter = uss820dci_device_isoc_fs_enter,
	.start = uss820dci_device_isoc_fs_start,
};

/*------------------------------------------------------------------------*
 * uss820dci root control support
 *------------------------------------------------------------------------*
 * Simulate a hardware HUB by handling all the necessary requests.
 *------------------------------------------------------------------------*/

static const struct usb_device_descriptor uss820dci_devd = {
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

static const struct usb_device_qualifier uss820dci_odevd = {
	.bLength = sizeof(struct usb_device_qualifier),
	.bDescriptorType = UDESC_DEVICE_QUALIFIER,
	.bcdUSB = {0x00, 0x02},
	.bDeviceClass = UDCLASS_HUB,
	.bDeviceSubClass = UDSUBCLASS_HUB,
	.bDeviceProtocol = UDPROTO_FSHUB,
	.bMaxPacketSize0 = 0,
	.bNumConfigurations = 0,
};

static const struct uss820dci_config_desc uss820dci_confd = {
	.confd = {
		.bLength = sizeof(struct usb_config_descriptor),
		.bDescriptorType = UDESC_CONFIG,
		.wTotalLength[0] = sizeof(uss820dci_confd),
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
		.bEndpointAddress = (UE_DIR_IN | USS820_DCI_INTR_ENDPT),
		.bmAttributes = UE_INTERRUPT,
		.wMaxPacketSize[0] = 8,
		.bInterval = 255,
	},
};

#define	HSETW(ptr, val) ptr = { (uint8_t)(val), (uint8_t)((val) >> 8) }

static const struct usb_hub_descriptor_min uss820dci_hubd = {
	.bDescLength = sizeof(uss820dci_hubd),
	.bDescriptorType = UDESC_HUB,
	.bNbrPorts = 1,
	HSETW(.wHubCharacteristics, (UHD_PWR_NO_SWITCH | UHD_OC_INDIVIDUAL)),
	.bPwrOn2PwrGood = 50,
	.bHubContrCurrent = 0,
	.DeviceRemovable = {0},		/* port is removable */
};

#define	STRING_VENDOR \
  "A\0G\0E\0R\0E"

#define	STRING_PRODUCT \
  "D\0C\0I\0 \0R\0o\0o\0t\0 \0H\0U\0B"

USB_MAKE_STRING_DESC(STRING_VENDOR, uss820dci_vendor);
USB_MAKE_STRING_DESC(STRING_PRODUCT, uss820dci_product);

static usb_error_t
uss820dci_roothub_exec(struct usb_device *udev,
    struct usb_device_request *req, const void **pptr, uint16_t *plength)
{
	struct uss820dci_softc *sc = USS820_DCI_BUS2SC(udev->bus);
	const void *ptr;
	uint16_t len;
	uint16_t value;
	uint16_t index;
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
		len = sizeof(uss820dci_devd);
		ptr = (const void *)&uss820dci_devd;
		goto tr_valid;
	case UDESC_DEVICE_QUALIFIER:
		if (value & 0xff) {
			goto tr_stalled;
		}
		len = sizeof(uss820dci_odevd);
		ptr = (const void *)&uss820dci_odevd;
		goto tr_valid;
	case UDESC_CONFIG:
		if (value & 0xff) {
			goto tr_stalled;
		}
		len = sizeof(uss820dci_confd);
		ptr = (const void *)&uss820dci_confd;
		goto tr_valid;
	case UDESC_STRING:
		switch (value & 0xff) {
		case 0:		/* Language table */
			len = sizeof(usb_string_lang_en);
			ptr = (const void *)&usb_string_lang_en;
			goto tr_valid;

		case 1:		/* Vendor */
			len = sizeof(uss820dci_vendor);
			ptr = (const void *)&uss820dci_vendor;
			goto tr_valid;

		case 2:		/* Product */
			len = sizeof(uss820dci_product);
			ptr = (const void *)&uss820dci_product;
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
		uss820dci_wakeup_peer(sc);
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
		uss820dci_pull_down(sc);
		break;
	case UHF_C_PORT_CONNECTION:
		sc->sc_flags.change_connect = 0;
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
		uss820dci_pull_up(sc);
	} else {
		uss820dci_pull_down(sc);
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
	ptr = (const void *)&uss820dci_hubd;
	len = sizeof(uss820dci_hubd);
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
uss820dci_xfer_setup(struct usb_setup_params *parm)
{
	const struct usb_hw_ep_profile *pf;
	struct uss820dci_softc *sc;
	struct usb_xfer *xfer;
	void *last_obj;
	uint32_t ntd;
	uint32_t n;
	uint8_t ep_no;

	sc = USS820_DCI_BUS2SC(parm->udev->bus);
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
	if (parm->methods == &uss820dci_device_ctrl_methods) {

		ntd = xfer->nframes + 1 /* STATUS */ + 1 /* SYNC */ ;

	} else if (parm->methods == &uss820dci_device_bulk_methods) {

		ntd = xfer->nframes + 1 /* SYNC */ ;

	} else if (parm->methods == &uss820dci_device_intr_methods) {

		ntd = xfer->nframes + 1 /* SYNC */ ;

	} else if (parm->methods == &uss820dci_device_isoc_fs_methods) {

		ntd = xfer->nframes + 1 /* SYNC */ ;

	} else {

		ntd = 0;
	}

	/*
	 * check if "usbd_transfer_setup_sub" set an error
	 */
	if (parm->err) {
		return;
	}
	/*
	 * allocate transfer descriptors
	 */
	last_obj = NULL;

	/*
	 * get profile stuff
	 */
	if (ntd) {

		ep_no = xfer->endpointno & UE_ADDR;
		uss820dci_get_hw_ep_profile(parm->udev, &pf, ep_no);

		if (pf == NULL) {
			/* should not happen */
			parm->err = USB_ERR_INVAL;
			return;
		}
	} else {
		ep_no = 0;
		pf = NULL;
	}

	/* align data */
	parm->size[0] += ((-parm->size[0]) & (USB_HOST_ALIGN - 1));

	for (n = 0; n != ntd; n++) {

		struct uss820dci_td *td;

		if (parm->buf) {

			td = USB_ADD_BYTES(parm->buf, parm->size[0]);

			/* init TD */
			td->max_packet_size = xfer->max_packet_size;
			td->ep_index = ep_no;
			if (pf->support_multi_buffer &&
			    (parm->methods != &uss820dci_device_ctrl_methods)) {
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
uss820dci_xfer_unsetup(struct usb_xfer *xfer)
{
	return;
}

static void
uss820dci_ep_init(struct usb_device *udev, struct usb_endpoint_descriptor *edesc,
    struct usb_endpoint *ep)
{
	struct uss820dci_softc *sc = USS820_DCI_BUS2SC(udev->bus);

	DPRINTFN(2, "endpoint=%p, addr=%d, endpt=%d, mode=%d (%d)\n",
	    ep, udev->address,
	    edesc->bEndpointAddress, udev->flags.usb_mode,
	    sc->sc_rt_addr);

	if (udev->device_index != sc->sc_rt_addr) {

		if (udev->speed != USB_SPEED_FULL) {
			/* not supported */
			return;
		}
		switch (edesc->bmAttributes & UE_XFERTYPE) {
		case UE_CONTROL:
			ep->methods = &uss820dci_device_ctrl_methods;
			break;
		case UE_INTERRUPT:
			ep->methods = &uss820dci_device_intr_methods;
			break;
		case UE_ISOCHRONOUS:
			ep->methods = &uss820dci_device_isoc_fs_methods;
			break;
		case UE_BULK:
			ep->methods = &uss820dci_device_bulk_methods;
			break;
		default:
			/* do nothing */
			break;
		}
	}
}

static void
uss820dci_set_hw_power_sleep(struct usb_bus *bus, uint32_t state)
{
	struct uss820dci_softc *sc = USS820_DCI_BUS2SC(bus);

	switch (state) {
	case USB_HW_POWER_SUSPEND:
		uss820dci_suspend(sc);
		break;
	case USB_HW_POWER_SHUTDOWN:
		uss820dci_uninit(sc);
		break;
	case USB_HW_POWER_RESUME:
		uss820dci_resume(sc);
		break;
	default:
		break;
	}
}

static const struct usb_bus_methods uss820dci_bus_methods =
{
	.endpoint_init = &uss820dci_ep_init,
	.xfer_setup = &uss820dci_xfer_setup,
	.xfer_unsetup = &uss820dci_xfer_unsetup,
	.get_hw_ep_profile = &uss820dci_get_hw_ep_profile,
	.xfer_stall = &uss820dci_xfer_stall,
	.set_stall = &uss820dci_set_stall,
	.clear_stall = &uss820dci_clear_stall,
	.roothub_exec = &uss820dci_roothub_exec,
	.xfer_poll = &uss820dci_do_poll,
	.set_hw_power_sleep = uss820dci_set_hw_power_sleep,
};
