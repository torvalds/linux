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

/*
 * Thanks to Mentor Graphics for providing a reference driver for this USB chip
 * at their homepage.
 */

/*
 * This file contains the driver for the Mentor Graphics Inventra USB
 * 2.0 High Speed Dual-Role controller.
 *
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

#define	USB_DEBUG_VAR musbotgdebug

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

#include <dev/usb/controller/musb_otg.h>

#define	MUSBOTG_INTR_ENDPT 1

#define	MUSBOTG_BUS2SC(bus) \
   ((struct musbotg_softc *)(((uint8_t *)(bus)) - \
   USB_P2U(&(((struct musbotg_softc *)0)->sc_bus))))

#define	MUSBOTG_PC2SC(pc) \
   MUSBOTG_BUS2SC(USB_DMATAG_TO_XROOT((pc)->tag_parent)->bus)

#ifdef USB_DEBUG
static int musbotgdebug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, musbotg, CTLFLAG_RW, 0, "USB musbotg");
SYSCTL_INT(_hw_usb_musbotg, OID_AUTO, debug, CTLFLAG_RWTUN,
    &musbotgdebug, 0, "Debug level");
#endif

#define	MAX_NAK_TO	16

/* prototypes */

static const struct usb_bus_methods musbotg_bus_methods;
static const struct usb_pipe_methods musbotg_device_bulk_methods;
static const struct usb_pipe_methods musbotg_device_ctrl_methods;
static const struct usb_pipe_methods musbotg_device_intr_methods;
static const struct usb_pipe_methods musbotg_device_isoc_methods;

/* Control transfers: Device mode */
static musbotg_cmd_t musbotg_dev_ctrl_setup_rx;
static musbotg_cmd_t musbotg_dev_ctrl_data_rx;
static musbotg_cmd_t musbotg_dev_ctrl_data_tx;
static musbotg_cmd_t musbotg_dev_ctrl_status;

/* Control transfers: Host mode */
static musbotg_cmd_t musbotg_host_ctrl_setup_tx;
static musbotg_cmd_t musbotg_host_ctrl_data_rx;
static musbotg_cmd_t musbotg_host_ctrl_data_tx;
static musbotg_cmd_t musbotg_host_ctrl_status_rx;
static musbotg_cmd_t musbotg_host_ctrl_status_tx;

/* Bulk, Interrupt, Isochronous: Device mode */
static musbotg_cmd_t musbotg_dev_data_rx;
static musbotg_cmd_t musbotg_dev_data_tx;

/* Bulk, Interrupt, Isochronous: Host mode */
static musbotg_cmd_t musbotg_host_data_rx;
static musbotg_cmd_t musbotg_host_data_tx;

static void	musbotg_device_done(struct usb_xfer *, usb_error_t);
static void	musbotg_do_poll(struct usb_bus *);
static void	musbotg_standard_done(struct usb_xfer *);
static void	musbotg_interrupt_poll(struct musbotg_softc *);
static void	musbotg_root_intr(struct musbotg_softc *);
static int	musbotg_channel_alloc(struct musbotg_softc *, struct musbotg_td *td, uint8_t);
static void	musbotg_channel_free(struct musbotg_softc *, struct musbotg_td *td);
static void	musbotg_ep_int_set(struct musbotg_softc *sc, int channel, int on);

/*
 * Here is a configuration that the chip supports.
 */
static const struct usb_hw_ep_profile musbotg_ep_profile[1] = {

	[0] = {
		.max_in_frame_size = 64,/* fixed */
		.max_out_frame_size = 64,	/* fixed */
		.is_simplex = 1,
		.support_control = 1,
	}
};

static const struct musb_otg_ep_cfg musbotg_ep_default[] = {
	{
		.ep_end = 1,
		.ep_fifosz_shift = 12,
		.ep_fifosz_reg = MUSB2_VAL_FIFOSZ_4096 | MUSB2_MASK_FIFODB,
	},
	{
		.ep_end = 7,
		.ep_fifosz_shift = 10,
		.ep_fifosz_reg = MUSB2_VAL_FIFOSZ_512 | MUSB2_MASK_FIFODB,
	},
	{
		.ep_end = 15,
		.ep_fifosz_shift = 7,
		.ep_fifosz_reg = MUSB2_VAL_FIFOSZ_128,
	},
	{
		.ep_end = -1,
	},
};

static int
musbotg_channel_alloc(struct musbotg_softc *sc, struct musbotg_td *td, uint8_t is_tx)
{
	int ch;
	int ep;

	ep = td->ep_no;

	/* In device mode each EP got its own channel */
	if (sc->sc_mode == MUSB2_DEVICE_MODE) {
		musbotg_ep_int_set(sc, ep, 1);
		return (ep);
	}

	/*
	 * All control transactions go through EP0
	 */
	if (ep == 0) {
		if (sc->sc_channel_mask & (1 << 0))
			return (-1);
		sc->sc_channel_mask |= (1 << 0);
		musbotg_ep_int_set(sc, ep, 1);
		return (0);
	}

	for (ch = sc->sc_ep_max; ch != 0; ch--) {
		if (sc->sc_channel_mask & (1 << ch))
			continue;

		/* check FIFO size requirement */
		if (is_tx) {
			if (td->max_frame_size >
			    sc->sc_hw_ep_profile[ch].max_in_frame_size)
				continue;
		} else {
			if (td->max_frame_size >
			    sc->sc_hw_ep_profile[ch].max_out_frame_size)
				continue;
		}
		sc->sc_channel_mask |= (1 << ch);
		musbotg_ep_int_set(sc, ch, 1);
		return (ch);
	}

	DPRINTFN(-1, "No available channels. Mask: %04x\n",  sc->sc_channel_mask);

	return (-1);
}

static void	
musbotg_channel_free(struct musbotg_softc *sc, struct musbotg_td *td)
{

	DPRINTFN(1, "ep_no=%d\n", td->channel);

	if (sc->sc_mode == MUSB2_DEVICE_MODE)
		return;

	if (td == NULL)
		return;
	if (td->channel == -1)
		return;

	musbotg_ep_int_set(sc, td->channel, 0);
	sc->sc_channel_mask &= ~(1 << td->channel);

	td->channel = -1;
}

static void
musbotg_get_hw_ep_profile(struct usb_device *udev,
    const struct usb_hw_ep_profile **ppf, uint8_t ep_addr)
{
	struct musbotg_softc *sc;

	sc = MUSBOTG_BUS2SC(udev->bus);

	if (ep_addr == 0) {
		/* control endpoint */
		*ppf = musbotg_ep_profile;
	} else if (ep_addr <= sc->sc_ep_max) {
		/* other endpoints */
		*ppf = sc->sc_hw_ep_profile + ep_addr;
	} else {
		*ppf = NULL;
	}
}

static void
musbotg_clocks_on(struct musbotg_softc *sc)
{
	if (sc->sc_flags.clocks_off &&
	    sc->sc_flags.port_powered) {

		DPRINTFN(4, "\n");

		if (sc->sc_clocks_on) {
			(sc->sc_clocks_on) (sc->sc_clocks_arg);
		}
		sc->sc_flags.clocks_off = 0;

		/* XXX enable Transceiver */
	}
}

static void
musbotg_clocks_off(struct musbotg_softc *sc)
{
	if (!sc->sc_flags.clocks_off) {

		DPRINTFN(4, "\n");

		/* XXX disable Transceiver */

		if (sc->sc_clocks_off) {
			(sc->sc_clocks_off) (sc->sc_clocks_arg);
		}
		sc->sc_flags.clocks_off = 1;
	}
}

static void
musbotg_pull_common(struct musbotg_softc *sc, uint8_t on)
{
	uint8_t temp;

	temp = MUSB2_READ_1(sc, MUSB2_REG_POWER);
	if (on)
		temp |= MUSB2_MASK_SOFTC;
	else
		temp &= ~MUSB2_MASK_SOFTC;

	MUSB2_WRITE_1(sc, MUSB2_REG_POWER, temp);
}

static void
musbotg_pull_up(struct musbotg_softc *sc)
{
	/* pullup D+, if possible */

	if (!sc->sc_flags.d_pulled_up &&
	    sc->sc_flags.port_powered) {
		sc->sc_flags.d_pulled_up = 1;
		musbotg_pull_common(sc, 1);
	}
}

static void
musbotg_pull_down(struct musbotg_softc *sc)
{
	/* pulldown D+, if possible */

	if (sc->sc_flags.d_pulled_up) {
		sc->sc_flags.d_pulled_up = 0;
		musbotg_pull_common(sc, 0);
	}
}

static void
musbotg_suspend_host(struct musbotg_softc *sc)
{
	uint8_t temp;

	if (sc->sc_flags.status_suspend) {
		return;
	}

	temp = MUSB2_READ_1(sc, MUSB2_REG_POWER);
	temp |= MUSB2_MASK_SUSPMODE;
	MUSB2_WRITE_1(sc, MUSB2_REG_POWER, temp);
	sc->sc_flags.status_suspend = 1;
}

static void
musbotg_wakeup_host(struct musbotg_softc *sc)
{
	uint8_t temp;

	if (!(sc->sc_flags.status_suspend)) {
		return;
	}

	temp = MUSB2_READ_1(sc, MUSB2_REG_POWER);
	temp &= ~MUSB2_MASK_SUSPMODE;
	temp |= MUSB2_MASK_RESUME;
	MUSB2_WRITE_1(sc, MUSB2_REG_POWER, temp);

	/* wait 20 milliseconds */
	/* Wait for reset to complete. */
	usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 50);

	temp = MUSB2_READ_1(sc, MUSB2_REG_POWER);
	temp &= ~MUSB2_MASK_RESUME;
	MUSB2_WRITE_1(sc, MUSB2_REG_POWER, temp);

	sc->sc_flags.status_suspend = 0;
}

static void
musbotg_wakeup_peer(struct musbotg_softc *sc)
{
	uint8_t temp;

	if (!(sc->sc_flags.status_suspend)) {
		return;
	}

	temp = MUSB2_READ_1(sc, MUSB2_REG_POWER);
	temp |= MUSB2_MASK_RESUME;
	MUSB2_WRITE_1(sc, MUSB2_REG_POWER, temp);

	/* wait 8 milliseconds */
	/* Wait for reset to complete. */
	usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 125);

	temp = MUSB2_READ_1(sc, MUSB2_REG_POWER);
	temp &= ~MUSB2_MASK_RESUME;
	MUSB2_WRITE_1(sc, MUSB2_REG_POWER, temp);
}

static void
musbotg_set_address(struct musbotg_softc *sc, uint8_t addr)
{
	DPRINTFN(4, "addr=%d\n", addr);
	addr &= 0x7F;
	MUSB2_WRITE_1(sc, MUSB2_REG_FADDR, addr);
}

static uint8_t
musbotg_dev_ctrl_setup_rx(struct musbotg_td *td)
{
	struct musbotg_softc *sc;
	struct usb_device_request req;
	uint16_t count;
	uint8_t csr;

	/* get pointer to softc */
	sc = MUSBOTG_PC2SC(td->pc);

	if (td->channel == -1)
		td->channel = musbotg_channel_alloc(sc, td, 0);

	/* EP0 is busy, wait */
	if (td->channel == -1)
		return (1);

	DPRINTFN(1, "ep_no=%d\n", td->channel);

	/* select endpoint 0 */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, 0);

	/* read out FIFO status */
	csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);

	DPRINTFN(4, "csr=0x%02x\n", csr);

	/*
	 * NOTE: If DATAEND is set we should not call the
	 * callback, hence the status stage is not complete.
	 */
	if (csr & MUSB2_MASK_CSR0L_DATAEND) {
		/* do not stall at this point */
		td->did_stall = 1;
		/* wait for interrupt */
		DPRINTFN(1, "CSR0 DATAEND\n");
		goto not_complete;
	}

	if (csr & MUSB2_MASK_CSR0L_SENTSTALL) {
		/* clear SENTSTALL */
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, 0);
		/* get latest status */
		csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
		/* update EP0 state */
		sc->sc_ep0_busy = 0;
	}
	if (csr & MUSB2_MASK_CSR0L_SETUPEND) {
		/* clear SETUPEND */
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
		    MUSB2_MASK_CSR0L_SETUPEND_CLR);
		/* get latest status */
		csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
		/* update EP0 state */
		sc->sc_ep0_busy = 0;
	}
	if (sc->sc_ep0_busy) {
		DPRINTFN(1, "EP0 BUSY\n");
		goto not_complete;
	}
	if (!(csr & MUSB2_MASK_CSR0L_RXPKTRDY)) {
		goto not_complete;
	}
	/* get the packet byte count */
	count = MUSB2_READ_2(sc, MUSB2_REG_RXCOUNT);

	/* verify data length */
	if (count != td->remainder) {
		DPRINTFN(1, "Invalid SETUP packet "
		    "length, %d bytes\n", count);
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
		      MUSB2_MASK_CSR0L_RXPKTRDY_CLR);
		/* don't clear stall */
		td->did_stall = 1;
		goto not_complete;
	}
	if (count != sizeof(req)) {
		DPRINTFN(1, "Unsupported SETUP packet "
		    "length, %d bytes\n", count);
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
		      MUSB2_MASK_CSR0L_RXPKTRDY_CLR);
		/* don't clear stall */
		td->did_stall = 1;
		goto not_complete;
	}
	/* clear did stall flag */
	td->did_stall = 0;

	/* receive data */
	bus_space_read_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
	    MUSB2_REG_EPFIFO(0), (void *)&req, sizeof(req));

	/* copy data into real buffer */
	usbd_copy_in(td->pc, 0, &req, sizeof(req));

	td->offset = sizeof(req);
	td->remainder = 0;

	/* set pending command */
	sc->sc_ep0_cmd = MUSB2_MASK_CSR0L_RXPKTRDY_CLR;

	/* we need set stall or dataend after this */
	sc->sc_ep0_busy = 1;

	/* sneak peek the set address */
	if ((req.bmRequestType == UT_WRITE_DEVICE) &&
	    (req.bRequest == UR_SET_ADDRESS)) {
		sc->sc_dv_addr = req.wValue[0] & 0x7F;
	} else {
		sc->sc_dv_addr = 0xFF;
	}

	musbotg_channel_free(sc, td);
	return (0);			/* complete */

not_complete:
	/* abort any ongoing transfer */
	if (!td->did_stall) {
		DPRINTFN(4, "stalling\n");
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
		    MUSB2_MASK_CSR0L_SENDSTALL);
		td->did_stall = 1;
	}
	return (1);			/* not complete */
}

static uint8_t
musbotg_host_ctrl_setup_tx(struct musbotg_td *td)
{
	struct musbotg_softc *sc;
	struct usb_device_request req;
	uint8_t csr, csrh;

	/* get pointer to softc */
	sc = MUSBOTG_PC2SC(td->pc);

	if (td->channel == -1)
		td->channel = musbotg_channel_alloc(sc, td, 1);

	/* EP0 is busy, wait */
	if (td->channel == -1)
		return (1);

	DPRINTFN(1, "ep_no=%d\n", td->channel);

	/* select endpoint 0 */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, 0);

	/* read out FIFO status */
	csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
	DPRINTFN(4, "csr=0x%02x\n", csr);

	/* Not ready yet yet */
	if (csr & MUSB2_MASK_CSR0L_TXPKTRDY)
		return (1);

	/* Failed */
	if (csr & (MUSB2_MASK_CSR0L_RXSTALL |
	    MUSB2_MASK_CSR0L_ERROR))
	{
		/* Clear status bit */
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, 0);
		DPRINTFN(1, "error bit set, csr=0x%02x\n", csr);
		td->error = 1;
	}

	if (csr & MUSB2_MASK_CSR0L_NAKTIMO) {
		DPRINTFN(1, "NAK timeout\n");

		if (csr & MUSB2_MASK_CSR0L_TXFIFONEMPTY) {
			csrh = MUSB2_READ_1(sc, MUSB2_REG_TXCSRH);
			csrh |= MUSB2_MASK_CSR0H_FFLUSH;
			MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRH, csrh);
			csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
			if (csr & MUSB2_MASK_CSR0L_TXFIFONEMPTY) {
				csrh = MUSB2_READ_1(sc, MUSB2_REG_TXCSRH);
				csrh |= MUSB2_MASK_CSR0H_FFLUSH;
				MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRH, csrh);
				csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
			}
		}

		csr &= ~MUSB2_MASK_CSR0L_NAKTIMO;
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, csr);

		td->error = 1;
	}

	if (td->error) {
		musbotg_channel_free(sc, td);
		return (0);
	}

	/* Fifo is not empty and there is no NAK timeout */
	if (csr & MUSB2_MASK_CSR0L_TXPKTRDY)
		return (1);

	/* check if we are complete */
	if (td->remainder == 0) {
		/* we are complete */
		musbotg_channel_free(sc, td);
		return (0);
	}

	/* copy data into real buffer */
	usbd_copy_out(td->pc, 0, &req, sizeof(req));

	/* send data */
	bus_space_write_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
	    MUSB2_REG_EPFIFO(0), (void *)&req, sizeof(req));

	/* update offset and remainder */
	td->offset += sizeof(req);
	td->remainder -= sizeof(req);


	MUSB2_WRITE_1(sc, MUSB2_REG_TXNAKLIMIT, MAX_NAK_TO);
	MUSB2_WRITE_1(sc, MUSB2_REG_TXFADDR(0), td->dev_addr);
	MUSB2_WRITE_1(sc, MUSB2_REG_TXHADDR(0), td->haddr);
	MUSB2_WRITE_1(sc, MUSB2_REG_TXHUBPORT(0), td->hport);
	MUSB2_WRITE_1(sc, MUSB2_REG_TXTI, td->transfer_type);

	/* write command */
	MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
	    MUSB2_MASK_CSR0L_TXPKTRDY | 
	    MUSB2_MASK_CSR0L_SETUPPKT);

	/* Just to be consistent, not used above */
	td->transaction_started = 1;

	return (1);			/* in progress */
}

/* Control endpoint only data handling functions (RX/TX/SYNC) */

static uint8_t
musbotg_dev_ctrl_data_rx(struct musbotg_td *td)
{
	struct usb_page_search buf_res;
	struct musbotg_softc *sc;
	uint16_t count;
	uint8_t csr;
	uint8_t got_short;

	/* get pointer to softc */
	sc = MUSBOTG_PC2SC(td->pc);

	/* select endpoint 0 */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, 0);

	/* check if a command is pending */
	if (sc->sc_ep0_cmd) {
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, sc->sc_ep0_cmd);
		sc->sc_ep0_cmd = 0;
	}
	/* read out FIFO status */
	csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);

	DPRINTFN(4, "csr=0x%02x\n", csr);

	got_short = 0;

	if (csr & (MUSB2_MASK_CSR0L_SETUPEND |
	    MUSB2_MASK_CSR0L_SENTSTALL)) {
		if (td->remainder == 0) {
			/*
			 * We are actually complete and have
			 * received the next SETUP
			 */
			DPRINTFN(4, "faking complete\n");
			return (0);	/* complete */
		}
		/*
	         * USB Host Aborted the transfer.
	         */
		td->error = 1;
		return (0);		/* complete */
	}
	if (!(csr & MUSB2_MASK_CSR0L_RXPKTRDY)) {
		return (1);		/* not complete */
	}
	/* get the packet byte count */
	count = MUSB2_READ_2(sc, MUSB2_REG_RXCOUNT);

	/* verify the packet byte count */
	if (count != td->max_frame_size) {
		if (count < td->max_frame_size) {
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
		uint32_t temp;

		usbd_get_page(td->pc, td->offset, &buf_res);

		/* get correct length */
		if (buf_res.length > count) {
			buf_res.length = count;
		}
		/* check for unaligned memory address */
		if (USB_P2U(buf_res.buffer) & 3) {

			temp = count & ~3;

			if (temp) {
				/* receive data 4 bytes at a time */
				bus_space_read_multi_4(sc->sc_io_tag, sc->sc_io_hdl,
				    MUSB2_REG_EPFIFO(0), sc->sc_bounce_buf,
				    temp / 4);
			}
			temp = count & 3;
			if (temp) {
				/* receive data 1 byte at a time */
				bus_space_read_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
				    MUSB2_REG_EPFIFO(0),
				    (void *)(&sc->sc_bounce_buf[count / 4]), temp);
			}
			usbd_copy_in(td->pc, td->offset,
			    sc->sc_bounce_buf, count);

			/* update offset and remainder */
			td->offset += count;
			td->remainder -= count;
			break;
		}
		/* check if we can optimise */
		if (buf_res.length >= 4) {

			/* receive data 4 bytes at a time */
			bus_space_read_multi_4(sc->sc_io_tag, sc->sc_io_hdl,
			    MUSB2_REG_EPFIFO(0), buf_res.buffer,
			    buf_res.length / 4);

			temp = buf_res.length & ~3;

			/* update counters */
			count -= temp;
			td->offset += temp;
			td->remainder -= temp;
			continue;
		}
		/* receive data */
		bus_space_read_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
		    MUSB2_REG_EPFIFO(0), buf_res.buffer, buf_res.length);

		/* update counters */
		count -= buf_res.length;
		td->offset += buf_res.length;
		td->remainder -= buf_res.length;
	}

	/* check if we are complete */
	if ((td->remainder == 0) || got_short) {
		if (td->short_pkt) {
			/* we are complete */
			sc->sc_ep0_cmd = MUSB2_MASK_CSR0L_RXPKTRDY_CLR;
			return (0);
		}
		/* else need to receive a zero length packet */
	}
	/* write command - need more data */
	MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
	    MUSB2_MASK_CSR0L_RXPKTRDY_CLR);
	return (1);			/* not complete */
}

static uint8_t
musbotg_dev_ctrl_data_tx(struct musbotg_td *td)
{
	struct usb_page_search buf_res;
	struct musbotg_softc *sc;
	uint16_t count;
	uint8_t csr;

	/* get pointer to softc */
	sc = MUSBOTG_PC2SC(td->pc);

	/* select endpoint 0 */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, 0);

	/* check if a command is pending */
	if (sc->sc_ep0_cmd) {
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, sc->sc_ep0_cmd);
		sc->sc_ep0_cmd = 0;
	}
	/* read out FIFO status */
	csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);

	DPRINTFN(4, "csr=0x%02x\n", csr);

	if (csr & (MUSB2_MASK_CSR0L_SETUPEND |
	    MUSB2_MASK_CSR0L_SENTSTALL)) {
		/*
	         * The current transfer was aborted
	         * by the USB Host
	         */
		td->error = 1;
		return (0);		/* complete */
	}
	if (csr & MUSB2_MASK_CSR0L_TXPKTRDY) {
		return (1);		/* not complete */
	}
	count = td->max_frame_size;
	if (td->remainder < count) {
		/* we have a short packet */
		td->short_pkt = 1;
		count = td->remainder;
	}
	while (count > 0) {
		uint32_t temp;

		usbd_get_page(td->pc, td->offset, &buf_res);

		/* get correct length */
		if (buf_res.length > count) {
			buf_res.length = count;
		}
		/* check for unaligned memory address */
		if (USB_P2U(buf_res.buffer) & 3) {

			usbd_copy_out(td->pc, td->offset,
			    sc->sc_bounce_buf, count);

			temp = count & ~3;

			if (temp) {
				/* transmit data 4 bytes at a time */
				bus_space_write_multi_4(sc->sc_io_tag, sc->sc_io_hdl,
				    MUSB2_REG_EPFIFO(0), sc->sc_bounce_buf,
				    temp / 4);
			}
			temp = count & 3;
			if (temp) {
				/* receive data 1 byte at a time */
				bus_space_write_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
				    MUSB2_REG_EPFIFO(0),
				    ((void *)&sc->sc_bounce_buf[count / 4]), temp);
			}
			/* update offset and remainder */
			td->offset += count;
			td->remainder -= count;
			break;
		}
		/* check if we can optimise */
		if (buf_res.length >= 4) {

			/* transmit data 4 bytes at a time */
			bus_space_write_multi_4(sc->sc_io_tag, sc->sc_io_hdl,
			    MUSB2_REG_EPFIFO(0), buf_res.buffer,
			    buf_res.length / 4);

			temp = buf_res.length & ~3;

			/* update counters */
			count -= temp;
			td->offset += temp;
			td->remainder -= temp;
			continue;
		}
		/* transmit data */
		bus_space_write_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
		    MUSB2_REG_EPFIFO(0), buf_res.buffer, buf_res.length);

		/* update counters */
		count -= buf_res.length;
		td->offset += buf_res.length;
		td->remainder -= buf_res.length;
	}

	/* check remainder */
	if (td->remainder == 0) {
		if (td->short_pkt) {
			sc->sc_ep0_cmd = MUSB2_MASK_CSR0L_TXPKTRDY;
			return (0);	/* complete */
		}
		/* else we need to transmit a short packet */
	}
	/* write command */
	MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
	    MUSB2_MASK_CSR0L_TXPKTRDY);

	return (1);			/* not complete */
}

static uint8_t
musbotg_host_ctrl_data_rx(struct musbotg_td *td)
{
	struct usb_page_search buf_res;
	struct musbotg_softc *sc;
	uint16_t count;
	uint8_t csr;
	uint8_t got_short;

	/* get pointer to softc */
	sc = MUSBOTG_PC2SC(td->pc);

	if (td->channel == -1)
		td->channel = musbotg_channel_alloc(sc, td, 0);

	/* EP0 is busy, wait */
	if (td->channel == -1)
		return (1);

	DPRINTFN(1, "ep_no=%d\n", td->channel);

	/* select endpoint 0 */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, 0);

	/* read out FIFO status */
	csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);

	DPRINTFN(4, "csr=0x%02x\n", csr);

	got_short = 0;
	if (!td->transaction_started) {
		td->transaction_started = 1;

		MUSB2_WRITE_1(sc, MUSB2_REG_RXNAKLIMIT, MAX_NAK_TO);

		MUSB2_WRITE_1(sc, MUSB2_REG_RXFADDR(0),
		    td->dev_addr);
		MUSB2_WRITE_1(sc, MUSB2_REG_RXHADDR(0), td->haddr);
		MUSB2_WRITE_1(sc, MUSB2_REG_RXHUBPORT(0), td->hport);
		MUSB2_WRITE_1(sc, MUSB2_REG_RXTI, td->transfer_type);

		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
		    MUSB2_MASK_CSR0L_REQPKT);

		return (1);
	}

	if (csr & MUSB2_MASK_CSR0L_NAKTIMO) {
		csr &= ~MUSB2_MASK_CSR0L_REQPKT;
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, csr);

		csr &= ~MUSB2_MASK_CSR0L_NAKTIMO;
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, csr);

		td->error = 1;
	}

	/* Failed */
	if (csr & (MUSB2_MASK_CSR0L_RXSTALL |
	    MUSB2_MASK_CSR0L_ERROR))
	{
		/* Clear status bit */
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, 0);
		DPRINTFN(1, "error bit set, csr=0x%02x\n", csr);
		td->error = 1;
	}

	if (td->error) {
		musbotg_channel_free(sc, td);
		return (0);	/* we are complete */
	}

	if (!(csr & MUSB2_MASK_CSR0L_RXPKTRDY))
		return (1); /* not yet */

	/* get the packet byte count */
	count = MUSB2_READ_2(sc, MUSB2_REG_RXCOUNT);

	/* verify the packet byte count */
	if (count != td->max_frame_size) {
		if (count < td->max_frame_size) {
			/* we have a short packet */
			td->short_pkt = 1;
			got_short = 1;
		} else {
			/* invalid USB packet */
			td->error = 1;
			musbotg_channel_free(sc, td);
			return (0);	/* we are complete */
		}
	}
	/* verify the packet byte count */
	if (count > td->remainder) {
		/* invalid USB packet */
		td->error = 1;
		musbotg_channel_free(sc, td);
		return (0);		/* we are complete */
	}
	while (count > 0) {
		uint32_t temp;

		usbd_get_page(td->pc, td->offset, &buf_res);

		/* get correct length */
		if (buf_res.length > count) {
			buf_res.length = count;
		}
		/* check for unaligned memory address */
		if (USB_P2U(buf_res.buffer) & 3) {

			temp = count & ~3;

			if (temp) {
				/* receive data 4 bytes at a time */
				bus_space_read_multi_4(sc->sc_io_tag, sc->sc_io_hdl,
				    MUSB2_REG_EPFIFO(0), sc->sc_bounce_buf,
				    temp / 4);
			}
			temp = count & 3;
			if (temp) {
				/* receive data 1 byte at a time */
				bus_space_read_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
				    MUSB2_REG_EPFIFO(0),
				    (void *)(&sc->sc_bounce_buf[count / 4]), temp);
			}
			usbd_copy_in(td->pc, td->offset,
			    sc->sc_bounce_buf, count);

			/* update offset and remainder */
			td->offset += count;
			td->remainder -= count;
			break;
		}
		/* check if we can optimise */
		if (buf_res.length >= 4) {

			/* receive data 4 bytes at a time */
			bus_space_read_multi_4(sc->sc_io_tag, sc->sc_io_hdl,
			    MUSB2_REG_EPFIFO(0), buf_res.buffer,
			    buf_res.length / 4);

			temp = buf_res.length & ~3;

			/* update counters */
			count -= temp;
			td->offset += temp;
			td->remainder -= temp;
			continue;
		}
		/* receive data */
		bus_space_read_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
		    MUSB2_REG_EPFIFO(0), buf_res.buffer, buf_res.length);

		/* update counters */
		count -= buf_res.length;
		td->offset += buf_res.length;
		td->remainder -= buf_res.length;
	}

	csr &= ~MUSB2_MASK_CSR0L_RXPKTRDY;
	MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, csr);

	/* check if we are complete */
	if ((td->remainder == 0) || got_short) {
		if (td->short_pkt) {
			/* we are complete */

			musbotg_channel_free(sc, td);
			return (0);
		}
		/* else need to receive a zero length packet */
	}

	td->transaction_started = 1;
	MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
	    MUSB2_MASK_CSR0L_REQPKT);

	return (1);			/* not complete */
}

static uint8_t
musbotg_host_ctrl_data_tx(struct musbotg_td *td)
{
	struct usb_page_search buf_res;
	struct musbotg_softc *sc;
	uint16_t count;
	uint8_t csr, csrh;

	/* get pointer to softc */
	sc = MUSBOTG_PC2SC(td->pc);

	if (td->channel == -1)
		td->channel = musbotg_channel_alloc(sc, td, 1);

	/* No free EPs */
	if (td->channel == -1)
		return (1);

	DPRINTFN(1, "ep_no=%d\n", td->channel);

	/* select endpoint */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, 0);

	/* read out FIFO status */
	csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
	DPRINTFN(4, "csr=0x%02x\n", csr);

	if (csr & (MUSB2_MASK_CSR0L_RXSTALL |
	    MUSB2_MASK_CSR0L_ERROR)) {
		/* clear status bits */
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, 0);
		td->error = 1;
	}

	if (csr & MUSB2_MASK_CSR0L_NAKTIMO ) {

		if (csr & MUSB2_MASK_CSR0L_TXFIFONEMPTY) {
			csrh = MUSB2_READ_1(sc, MUSB2_REG_TXCSRH);
			csrh |= MUSB2_MASK_CSR0H_FFLUSH;
			MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRH, csrh);
			csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
			if (csr & MUSB2_MASK_CSR0L_TXFIFONEMPTY) {
				csrh = MUSB2_READ_1(sc, MUSB2_REG_TXCSRH);
				csrh |= MUSB2_MASK_CSR0H_FFLUSH;
				MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRH, csrh);
				csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
			}
		}

		csr &= ~MUSB2_MASK_CSR0L_NAKTIMO;
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, csr);

		td->error = 1;
	}


	if (td->error) {
		musbotg_channel_free(sc, td);
		return (0);	/* complete */
	}

	/*
	 * Wait while FIFO is empty. 
	 * Do not flush it because it will cause transactions
	 * with size more then packet size. It might upset
	 * some devices
	 */
	if (csr & MUSB2_MASK_CSR0L_TXFIFONEMPTY)
		return (1);

	/* Packet still being processed */
	if (csr & MUSB2_MASK_CSR0L_TXPKTRDY)
		return (1);

	if (td->transaction_started) {
		/* check remainder */
		if (td->remainder == 0) {
			if (td->short_pkt) {
				musbotg_channel_free(sc, td);
				return (0);	/* complete */
			}
			/* else we need to transmit a short packet */
		}

		/* We're not complete - more transactions required */
		td->transaction_started = 0;
	}

	/* check for short packet */
	count = td->max_frame_size;
	if (td->remainder < count) {
		/* we have a short packet */
		td->short_pkt = 1;
		count = td->remainder;
	}

	while (count > 0) {
		uint32_t temp;

		usbd_get_page(td->pc, td->offset, &buf_res);

		/* get correct length */
		if (buf_res.length > count) {
			buf_res.length = count;
		}
		/* check for unaligned memory address */
		if (USB_P2U(buf_res.buffer) & 3) {

			usbd_copy_out(td->pc, td->offset,
			    sc->sc_bounce_buf, count);

			temp = count & ~3;

			if (temp) {
				/* transmit data 4 bytes at a time */
				bus_space_write_multi_4(sc->sc_io_tag,
				    sc->sc_io_hdl, MUSB2_REG_EPFIFO(0),
				    sc->sc_bounce_buf, temp / 4);
			}
			temp = count & 3;
			if (temp) {
				/* receive data 1 byte at a time */
				bus_space_write_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
				    MUSB2_REG_EPFIFO(0),
				    ((void *)&sc->sc_bounce_buf[count / 4]), temp);
			}
			/* update offset and remainder */
			td->offset += count;
			td->remainder -= count;
			break;
		}
		/* check if we can optimise */
		if (buf_res.length >= 4) {

			/* transmit data 4 bytes at a time */
			bus_space_write_multi_4(sc->sc_io_tag, sc->sc_io_hdl,
			    MUSB2_REG_EPFIFO(0), buf_res.buffer,
			    buf_res.length / 4);

			temp = buf_res.length & ~3;

			/* update counters */
			count -= temp;
			td->offset += temp;
			td->remainder -= temp;
			continue;
		}
		/* transmit data */
		bus_space_write_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
		    MUSB2_REG_EPFIFO(0), buf_res.buffer,
		    buf_res.length);

		/* update counters */
		count -= buf_res.length;
		td->offset += buf_res.length;
		td->remainder -= buf_res.length;
	}

	/* Function address */
	MUSB2_WRITE_1(sc, MUSB2_REG_TXFADDR(0), td->dev_addr);
	MUSB2_WRITE_1(sc, MUSB2_REG_TXHADDR(0), td->haddr);
	MUSB2_WRITE_1(sc, MUSB2_REG_TXHUBPORT(0), td->hport);
	MUSB2_WRITE_1(sc, MUSB2_REG_TXTI, td->transfer_type);

	/* TX NAK timeout */
	MUSB2_WRITE_1(sc, MUSB2_REG_TXNAKLIMIT, MAX_NAK_TO);

	/* write command */
	MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
	    MUSB2_MASK_CSR0L_TXPKTRDY);

	td->transaction_started = 1;

	return (1);			/* not complete */
}

static uint8_t
musbotg_dev_ctrl_status(struct musbotg_td *td)
{
	struct musbotg_softc *sc;
	uint8_t csr;

	/* get pointer to softc */
	sc = MUSBOTG_PC2SC(td->pc);

	/* select endpoint 0 */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, 0);

	if (sc->sc_ep0_busy) {
		sc->sc_ep0_busy = 0;
		sc->sc_ep0_cmd |= MUSB2_MASK_CSR0L_DATAEND;
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, sc->sc_ep0_cmd);
		sc->sc_ep0_cmd = 0;
	}
	/* read out FIFO status */
	csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);

	DPRINTFN(4, "csr=0x%02x\n", csr);

	if (csr & MUSB2_MASK_CSR0L_DATAEND) {
		/* wait for interrupt */
		return (1);		/* not complete */
	}
	if (sc->sc_dv_addr != 0xFF) {
		/* write function address */
		musbotg_set_address(sc, sc->sc_dv_addr);
	}

	musbotg_channel_free(sc, td);
	return (0);			/* complete */
}

static uint8_t
musbotg_host_ctrl_status_rx(struct musbotg_td *td)
{
	struct musbotg_softc *sc;
	uint8_t csr, csrh;

	/* get pointer to softc */
	sc = MUSBOTG_PC2SC(td->pc);

	if (td->channel == -1)
		td->channel = musbotg_channel_alloc(sc, td, 0);

	/* EP0 is busy, wait */
	if (td->channel == -1)
		return (1);

	DPRINTFN(1, "ep_no=%d\n", td->channel);

	/* select endpoint 0 */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, 0);

	if (!td->transaction_started) {
		MUSB2_WRITE_1(sc, MUSB2_REG_RXFADDR(0),
		    td->dev_addr);

		MUSB2_WRITE_1(sc, MUSB2_REG_RXHADDR(0), td->haddr);
		MUSB2_WRITE_1(sc, MUSB2_REG_RXHUBPORT(0), td->hport);
		MUSB2_WRITE_1(sc, MUSB2_REG_RXTI, td->transfer_type);

		/* RX NAK timeout */
		MUSB2_WRITE_1(sc, MUSB2_REG_RXNAKLIMIT, MAX_NAK_TO);

		td->transaction_started = 1;

		/* Disable PING */
		csrh = MUSB2_READ_1(sc, MUSB2_REG_RXCSRH);
		csrh |= MUSB2_MASK_CSR0H_PING_DIS;
		MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRH, csrh);

		/* write command */
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
		    MUSB2_MASK_CSR0L_STATUSPKT | 
		    MUSB2_MASK_CSR0L_REQPKT);

		return (1); /* Just started */

	}

	csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);

	DPRINTFN(4, "IN STATUS csr=0x%02x\n", csr);

	if (csr & MUSB2_MASK_CSR0L_RXPKTRDY) {
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
		    MUSB2_MASK_CSR0L_RXPKTRDY_CLR);
		musbotg_channel_free(sc, td);
		return (0); /* complete */
	}

	if (csr & MUSB2_MASK_CSR0L_NAKTIMO) {
		csr &= ~ (MUSB2_MASK_CSR0L_STATUSPKT |
		    MUSB2_MASK_CSR0L_REQPKT);
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, csr);

		csr &= ~MUSB2_MASK_CSR0L_NAKTIMO;
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, csr);
		td->error = 1;
	}

	/* Failed */
	if (csr & (MUSB2_MASK_CSR0L_RXSTALL |
	    MUSB2_MASK_CSR0L_ERROR))
	{
		/* Clear status bit */
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, 0);
		DPRINTFN(1, "error bit set, csr=0x%02x\n", csr);
		td->error = 1;
	}

	if (td->error) {
		musbotg_channel_free(sc, td);
		return (0);
	}

	return (1);			/* Not ready yet */
}

static uint8_t
musbotg_host_ctrl_status_tx(struct musbotg_td *td)
{
	struct musbotg_softc *sc;
	uint8_t csr;

	/* get pointer to softc */
	sc = MUSBOTG_PC2SC(td->pc);

	if (td->channel == -1)
		td->channel = musbotg_channel_alloc(sc, td, 1);

	/* EP0 is busy, wait */
	if (td->channel == -1)
		return (1);

	DPRINTFN(1, "ep_no=%d/%d [%d@%d.%d/%02x]\n", td->channel, td->transaction_started, 
			td->dev_addr,td->haddr,td->hport, td->transfer_type);

	/* select endpoint 0 */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, 0);

	csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
	DPRINTFN(4, "csr=0x%02x\n", csr);

	/* Not yet */
	if (csr & MUSB2_MASK_CSR0L_TXPKTRDY)
		return (1);

	/* Failed */
	if (csr & (MUSB2_MASK_CSR0L_RXSTALL |
	    MUSB2_MASK_CSR0L_ERROR))
	{
		/* Clear status bit */
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, 0);
		DPRINTFN(1, "error bit set, csr=0x%02x\n", csr);
		td->error = 1;
		musbotg_channel_free(sc, td);
		return (0); /* complete */
	}

	if (td->transaction_started) {
		musbotg_channel_free(sc, td);
		return (0); /* complete */
	} 

	MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRH, MUSB2_MASK_CSR0H_PING_DIS);

	MUSB2_WRITE_1(sc, MUSB2_REG_TXFADDR(0), td->dev_addr);
	MUSB2_WRITE_1(sc, MUSB2_REG_TXHADDR(0), td->haddr);
	MUSB2_WRITE_1(sc, MUSB2_REG_TXHUBPORT(0), td->hport);
	MUSB2_WRITE_1(sc, MUSB2_REG_TXTI, td->transfer_type);

	/* TX NAK timeout */
	MUSB2_WRITE_1(sc, MUSB2_REG_TXNAKLIMIT, MAX_NAK_TO);

	td->transaction_started = 1;

	/* write command */
	MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
	    MUSB2_MASK_CSR0L_STATUSPKT | 
	    MUSB2_MASK_CSR0L_TXPKTRDY);

	return (1);			/* wait for interrupt */
}

static uint8_t
musbotg_dev_data_rx(struct musbotg_td *td)
{
	struct usb_page_search buf_res;
	struct musbotg_softc *sc;
	uint16_t count;
	uint8_t csr;
	uint8_t to;
	uint8_t got_short;

	to = 8;				/* don't loop forever! */
	got_short = 0;

	/* get pointer to softc */
	sc = MUSBOTG_PC2SC(td->pc);

	if (td->channel == -1)
		td->channel = musbotg_channel_alloc(sc, td, 0);

	/* EP0 is busy, wait */
	if (td->channel == -1)
		return (1);

	/* select endpoint */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, td->channel);

repeat:
	/* read out FIFO status */
	csr = MUSB2_READ_1(sc, MUSB2_REG_RXCSRL);

	DPRINTFN(4, "csr=0x%02x\n", csr);

	/* clear overrun */
	if (csr & MUSB2_MASK_CSRL_RXOVERRUN) {
		/* make sure we don't clear "RXPKTRDY" */
		MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRL,
		    MUSB2_MASK_CSRL_RXPKTRDY);
	}

	/* check status */
	if (!(csr & MUSB2_MASK_CSRL_RXPKTRDY))
		return (1); /* not complete */

	/* get the packet byte count */
	count = MUSB2_READ_2(sc, MUSB2_REG_RXCOUNT);

	DPRINTFN(4, "count=0x%04x\n", count);

	/*
	 * Check for short or invalid packet:
	 */
	if (count != td->max_frame_size) {
		if (count < td->max_frame_size) {
			/* we have a short packet */
			td->short_pkt = 1;
			got_short = 1;
		} else {
			/* invalid USB packet */
			td->error = 1;
			musbotg_channel_free(sc, td);
			return (0);	/* we are complete */
		}
	}
	/* verify the packet byte count */
	if (count > td->remainder) {
		/* invalid USB packet */
		td->error = 1;
		musbotg_channel_free(sc, td);
		return (0);		/* we are complete */
	}
	while (count > 0) {
		uint32_t temp;

		usbd_get_page(td->pc, td->offset, &buf_res);

		/* get correct length */
		if (buf_res.length > count) {
			buf_res.length = count;
		}
		/* check for unaligned memory address */
		if (USB_P2U(buf_res.buffer) & 3) {

			temp = count & ~3;

			if (temp) {
				/* receive data 4 bytes at a time */
				bus_space_read_multi_4(sc->sc_io_tag, sc->sc_io_hdl,
				    MUSB2_REG_EPFIFO(td->channel), sc->sc_bounce_buf,
				    temp / 4);
			}
			temp = count & 3;
			if (temp) {
				/* receive data 1 byte at a time */
				bus_space_read_multi_1(sc->sc_io_tag,
				    sc->sc_io_hdl, MUSB2_REG_EPFIFO(td->channel),
				    ((void *)&sc->sc_bounce_buf[count / 4]), temp);
			}
			usbd_copy_in(td->pc, td->offset,
			    sc->sc_bounce_buf, count);

			/* update offset and remainder */
			td->offset += count;
			td->remainder -= count;
			break;
		}
		/* check if we can optimise */
		if (buf_res.length >= 4) {

			/* receive data 4 bytes at a time */
			bus_space_read_multi_4(sc->sc_io_tag, sc->sc_io_hdl,
			    MUSB2_REG_EPFIFO(td->channel), buf_res.buffer,
			    buf_res.length / 4);

			temp = buf_res.length & ~3;

			/* update counters */
			count -= temp;
			td->offset += temp;
			td->remainder -= temp;
			continue;
		}
		/* receive data */
		bus_space_read_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
		    MUSB2_REG_EPFIFO(td->channel), buf_res.buffer,
		    buf_res.length);

		/* update counters */
		count -= buf_res.length;
		td->offset += buf_res.length;
		td->remainder -= buf_res.length;
	}

	/* clear status bits */
	MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRL, 0);

	/* check if we are complete */
	if ((td->remainder == 0) || got_short) {
		if (td->short_pkt) {
			/* we are complete */
			musbotg_channel_free(sc, td);
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
musbotg_dev_data_tx(struct musbotg_td *td)
{
	struct usb_page_search buf_res;
	struct musbotg_softc *sc;
	uint16_t count;
	uint8_t csr;
	uint8_t to;

	to = 8;				/* don't loop forever! */

	/* get pointer to softc */
	sc = MUSBOTG_PC2SC(td->pc);

	if (td->channel == -1)
		td->channel = musbotg_channel_alloc(sc, td, 1);

	/* EP0 is busy, wait */
	if (td->channel == -1)
		return (1);

	/* select endpoint */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, td->channel);

repeat:

	/* read out FIFO status */
	csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);

	DPRINTFN(4, "csr=0x%02x\n", csr);

	if (csr & (MUSB2_MASK_CSRL_TXINCOMP |
	    MUSB2_MASK_CSRL_TXUNDERRUN)) {
		/* clear status bits */
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, 0);
	}
	if (csr & MUSB2_MASK_CSRL_TXPKTRDY) {
		return (1);		/* not complete */
	}
	/* check for short packet */
	count = td->max_frame_size;
	if (td->remainder < count) {
		/* we have a short packet */
		td->short_pkt = 1;
		count = td->remainder;
	}
	while (count > 0) {
		uint32_t temp;

		usbd_get_page(td->pc, td->offset, &buf_res);

		/* get correct length */
		if (buf_res.length > count) {
			buf_res.length = count;
		}
		/* check for unaligned memory address */
		if (USB_P2U(buf_res.buffer) & 3) {

			usbd_copy_out(td->pc, td->offset,
			    sc->sc_bounce_buf, count);

			temp = count & ~3;

			if (temp) {
				/* transmit data 4 bytes at a time */
				bus_space_write_multi_4(sc->sc_io_tag,
				    sc->sc_io_hdl, MUSB2_REG_EPFIFO(td->channel),
				    sc->sc_bounce_buf, temp / 4);
			}
			temp = count & 3;
			if (temp) {
				/* receive data 1 byte at a time */
				bus_space_write_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
				    MUSB2_REG_EPFIFO(td->channel),
				    ((void *)&sc->sc_bounce_buf[count / 4]), temp);
			}
			/* update offset and remainder */
			td->offset += count;
			td->remainder -= count;
			break;
		}
		/* check if we can optimise */
		if (buf_res.length >= 4) {

			/* transmit data 4 bytes at a time */
			bus_space_write_multi_4(sc->sc_io_tag, sc->sc_io_hdl,
			    MUSB2_REG_EPFIFO(td->channel), buf_res.buffer,
			    buf_res.length / 4);

			temp = buf_res.length & ~3;

			/* update counters */
			count -= temp;
			td->offset += temp;
			td->remainder -= temp;
			continue;
		}
		/* transmit data */
		bus_space_write_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
		    MUSB2_REG_EPFIFO(td->channel), buf_res.buffer,
		    buf_res.length);

		/* update counters */
		count -= buf_res.length;
		td->offset += buf_res.length;
		td->remainder -= buf_res.length;
	}

	/* Max packet size */
	MUSB2_WRITE_2(sc, MUSB2_REG_TXMAXP, td->reg_max_packet);

	/* write command */
	MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
	    MUSB2_MASK_CSRL_TXPKTRDY);

	/* check remainder */
	if (td->remainder == 0) {
		if (td->short_pkt) {
			musbotg_channel_free(sc, td);
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
musbotg_host_data_rx(struct musbotg_td *td)
{
	struct usb_page_search buf_res;
	struct musbotg_softc *sc;
	uint16_t count;
	uint8_t csr, csrh;
	uint8_t to;
	uint8_t got_short;

	/* get pointer to softc */
	sc = MUSBOTG_PC2SC(td->pc);

	if (td->channel == -1)
		td->channel = musbotg_channel_alloc(sc, td, 0);

	/* No free EPs */
	if (td->channel == -1)
		return (1);

	DPRINTFN(1, "ep_no=%d\n", td->channel);

	to = 8;				/* don't loop forever! */
	got_short = 0;

	/* select endpoint */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, td->channel);

repeat:
	/* read out FIFO status */
	csr = MUSB2_READ_1(sc, MUSB2_REG_RXCSRL);
	DPRINTFN(4, "csr=0x%02x\n", csr);

	if (!td->transaction_started) {
		/* Function address */
		MUSB2_WRITE_1(sc, MUSB2_REG_RXFADDR(td->channel),
		    td->dev_addr);

		/* SPLIT transaction */
		MUSB2_WRITE_1(sc, MUSB2_REG_RXHADDR(td->channel), 
		    td->haddr);
		MUSB2_WRITE_1(sc, MUSB2_REG_RXHUBPORT(td->channel), 
		    td->hport);

		/* RX NAK timeout */
		if (td->transfer_type & MUSB2_MASK_TI_PROTO_ISOC)
			MUSB2_WRITE_1(sc, MUSB2_REG_RXNAKLIMIT, 0);
		else
			MUSB2_WRITE_1(sc, MUSB2_REG_RXNAKLIMIT, MAX_NAK_TO);

		/* Protocol, speed, device endpoint */
		MUSB2_WRITE_1(sc, MUSB2_REG_RXTI, td->transfer_type);

		/* Max packet size */
		MUSB2_WRITE_2(sc, MUSB2_REG_RXMAXP, td->reg_max_packet);

		/* Data Toggle */
		csrh = MUSB2_READ_1(sc, MUSB2_REG_RXCSRH);
		DPRINTFN(4, "csrh=0x%02x\n", csrh);

		csrh |= MUSB2_MASK_CSRH_RXDT_WREN;
		if (td->toggle)
			csrh |= MUSB2_MASK_CSRH_RXDT_VAL;
		else
			csrh &= ~MUSB2_MASK_CSRH_RXDT_VAL;

		/* Set data toggle */
		MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRH, csrh);

		/* write command */
		MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRL,
		    MUSB2_MASK_CSRL_RXREQPKT);

		td->transaction_started = 1;
		return (1);
	}

	/* clear NAK timeout */
	if (csr & MUSB2_MASK_CSRL_RXNAKTO) {
		DPRINTFN(4, "NAK Timeout\n");
		if (csr & MUSB2_MASK_CSRL_RXREQPKT) {
			csr &= ~MUSB2_MASK_CSRL_RXREQPKT;
			MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRL, csr);

			csr &= ~MUSB2_MASK_CSRL_RXNAKTO;
			MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRL, csr);
		}

		td->error = 1;
	}

	if (csr & MUSB2_MASK_CSRL_RXERROR) {
		DPRINTFN(4, "RXERROR\n");
		td->error = 1;
	}

	if (csr & MUSB2_MASK_CSRL_RXSTALL) {
		DPRINTFN(4, "RXSTALL\n");
		td->error = 1;
	}

	if (td->error) {
		musbotg_channel_free(sc, td);
		return (0);	/* we are complete */
	}

	if (!(csr & MUSB2_MASK_CSRL_RXPKTRDY)) {
		/* No data available yet */
		return (1);
	}

	td->toggle ^= 1;
	/* get the packet byte count */
	count = MUSB2_READ_2(sc, MUSB2_REG_RXCOUNT);
	DPRINTFN(4, "count=0x%04x\n", count);

	/*
	 * Check for short or invalid packet:
	 */
	if (count != td->max_frame_size) {
		if (count < td->max_frame_size) {
			/* we have a short packet */
			td->short_pkt = 1;
			got_short = 1;
		} else {
			/* invalid USB packet */
			td->error = 1;
			musbotg_channel_free(sc, td);
			return (0);	/* we are complete */
		}
	}

	/* verify the packet byte count */
	if (count > td->remainder) {
		/* invalid USB packet */
		td->error = 1;
		musbotg_channel_free(sc, td);
		return (0);		/* we are complete */
	}

	while (count > 0) {
		uint32_t temp;

		usbd_get_page(td->pc, td->offset, &buf_res);

		/* get correct length */
		if (buf_res.length > count) {
			buf_res.length = count;
		}
		/* check for unaligned memory address */
		if (USB_P2U(buf_res.buffer) & 3) {

			temp = count & ~3;

			if (temp) {
				/* receive data 4 bytes at a time */
				bus_space_read_multi_4(sc->sc_io_tag, sc->sc_io_hdl,
				    MUSB2_REG_EPFIFO(td->channel), sc->sc_bounce_buf,
				    temp / 4);
			}
			temp = count & 3;
			if (temp) {
				/* receive data 1 byte at a time */
				bus_space_read_multi_1(sc->sc_io_tag,
				    sc->sc_io_hdl, MUSB2_REG_EPFIFO(td->channel),
				    ((void *)&sc->sc_bounce_buf[count / 4]), temp);
			}
			usbd_copy_in(td->pc, td->offset,
			    sc->sc_bounce_buf, count);

			/* update offset and remainder */
			td->offset += count;
			td->remainder -= count;
			break;
		}
		/* check if we can optimise */
		if (buf_res.length >= 4) {

			/* receive data 4 bytes at a time */
			bus_space_read_multi_4(sc->sc_io_tag, sc->sc_io_hdl,
			    MUSB2_REG_EPFIFO(td->channel), buf_res.buffer,
			    buf_res.length / 4);

			temp = buf_res.length & ~3;

			/* update counters */
			count -= temp;
			td->offset += temp;
			td->remainder -= temp;
			continue;
		}
		/* receive data */
		bus_space_read_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
		    MUSB2_REG_EPFIFO(td->channel), buf_res.buffer,
		    buf_res.length);

		/* update counters */
		count -= buf_res.length;
		td->offset += buf_res.length;
		td->remainder -= buf_res.length;
	}

	/* clear status bits */
	MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRL, 0);

	/* check if we are complete */
	if ((td->remainder == 0) || got_short) {
		if (td->short_pkt) {
			/* we are complete */
			musbotg_channel_free(sc, td);
			return (0);
		}
		/* else need to receive a zero length packet */
	}

	/* Reset transaction state and restart */
	td->transaction_started = 0;

	if (--to)
		goto repeat;

	return (1);			/* not complete */
}

static uint8_t
musbotg_host_data_tx(struct musbotg_td *td)
{
	struct usb_page_search buf_res;
	struct musbotg_softc *sc;
	uint16_t count;
	uint8_t csr, csrh;

	/* get pointer to softc */
	sc = MUSBOTG_PC2SC(td->pc);

	if (td->channel == -1)
		td->channel = musbotg_channel_alloc(sc, td, 1);

	/* No free EPs */
	if (td->channel == -1)
		return (1);

	DPRINTFN(1, "ep_no=%d\n", td->channel);

	/* select endpoint */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, td->channel);

	/* read out FIFO status */
	csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
	DPRINTFN(4, "csr=0x%02x\n", csr);

	if (csr & (MUSB2_MASK_CSRL_TXSTALLED |
	    MUSB2_MASK_CSRL_TXERROR)) {
		/* clear status bits */
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, 0);
		td->error = 1;
		musbotg_channel_free(sc, td);
		return (0);	/* complete */
	}

	if (csr & MUSB2_MASK_CSRL_TXNAKTO) {
		/* 
		 * Flush TX FIFO before clearing NAK TO
		 */
		if (csr & MUSB2_MASK_CSRL_TXFIFONEMPTY) {
			csr |= MUSB2_MASK_CSRL_TXFFLUSH;
			MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, csr);
			csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
			if (csr & MUSB2_MASK_CSRL_TXFIFONEMPTY) {
				csr |= MUSB2_MASK_CSRL_TXFFLUSH;
				MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, csr);
				csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
			}
		}

		csr &= ~MUSB2_MASK_CSRL_TXNAKTO;
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, csr);

		td->error = 1;
		musbotg_channel_free(sc, td);
		return (0);	/* complete */
	}

	/*
	 * Wait while FIFO is empty. 
	 * Do not flush it because it will cause transactions
	 * with size more then packet size. It might upset
	 * some devices
	 */
	if (csr & MUSB2_MASK_CSRL_TXFIFONEMPTY)
		return (1);

	/* Packet still being processed */
	if (csr & MUSB2_MASK_CSRL_TXPKTRDY)
		return (1);

	if (td->transaction_started) {
		/* check remainder */
		if (td->remainder == 0) {
			if (td->short_pkt) {
				musbotg_channel_free(sc, td);
				return (0);	/* complete */
			}
			/* else we need to transmit a short packet */
		}

		/* We're not complete - more transactions required */
		td->transaction_started = 0;
	}

	/* check for short packet */
	count = td->max_frame_size;
	if (td->remainder < count) {
		/* we have a short packet */
		td->short_pkt = 1;
		count = td->remainder;
	}

	while (count > 0) {
		uint32_t temp;

		usbd_get_page(td->pc, td->offset, &buf_res);

		/* get correct length */
		if (buf_res.length > count) {
			buf_res.length = count;
		}
		/* check for unaligned memory address */
		if (USB_P2U(buf_res.buffer) & 3) {

			usbd_copy_out(td->pc, td->offset,
			    sc->sc_bounce_buf, count);

			temp = count & ~3;

			if (temp) {
				/* transmit data 4 bytes at a time */
				bus_space_write_multi_4(sc->sc_io_tag,
				    sc->sc_io_hdl, MUSB2_REG_EPFIFO(td->channel),
				    sc->sc_bounce_buf, temp / 4);
			}
			temp = count & 3;
			if (temp) {
				/* receive data 1 byte at a time */
				bus_space_write_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
				    MUSB2_REG_EPFIFO(td->channel),
				    ((void *)&sc->sc_bounce_buf[count / 4]), temp);
			}
			/* update offset and remainder */
			td->offset += count;
			td->remainder -= count;
			break;
		}
		/* check if we can optimise */
		if (buf_res.length >= 4) {

			/* transmit data 4 bytes at a time */
			bus_space_write_multi_4(sc->sc_io_tag, sc->sc_io_hdl,
			    MUSB2_REG_EPFIFO(td->channel), buf_res.buffer,
			    buf_res.length / 4);

			temp = buf_res.length & ~3;

			/* update counters */
			count -= temp;
			td->offset += temp;
			td->remainder -= temp;
			continue;
		}
		/* transmit data */
		bus_space_write_multi_1(sc->sc_io_tag, sc->sc_io_hdl,
		    MUSB2_REG_EPFIFO(td->channel), buf_res.buffer,
		    buf_res.length);

		/* update counters */
		count -= buf_res.length;
		td->offset += buf_res.length;
		td->remainder -= buf_res.length;
	}

	/* Function address */
	MUSB2_WRITE_1(sc, MUSB2_REG_TXFADDR(td->channel),
	    td->dev_addr);

	/* SPLIT transaction */
	MUSB2_WRITE_1(sc, MUSB2_REG_TXHADDR(td->channel), 
	    td->haddr);
	MUSB2_WRITE_1(sc, MUSB2_REG_TXHUBPORT(td->channel), 
	    td->hport);

	/* TX NAK timeout */
	if (td->transfer_type & MUSB2_MASK_TI_PROTO_ISOC)
		MUSB2_WRITE_1(sc, MUSB2_REG_TXNAKLIMIT, 0);
	else
		MUSB2_WRITE_1(sc, MUSB2_REG_TXNAKLIMIT, MAX_NAK_TO);

	/* Protocol, speed, device endpoint */
	MUSB2_WRITE_1(sc, MUSB2_REG_TXTI, td->transfer_type);

	/* Max packet size */
	MUSB2_WRITE_2(sc, MUSB2_REG_TXMAXP, td->reg_max_packet);

	if (!td->transaction_started) {
		csrh = MUSB2_READ_1(sc, MUSB2_REG_TXCSRH);
		DPRINTFN(4, "csrh=0x%02x\n", csrh);

		csrh |= MUSB2_MASK_CSRH_TXDT_WREN;
		if (td->toggle)
			csrh |= MUSB2_MASK_CSRH_TXDT_VAL;
		else
			csrh &= ~MUSB2_MASK_CSRH_TXDT_VAL;

		/* Set data toggle */
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRH, csrh);
	}

	/* write command */
	MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
	    MUSB2_MASK_CSRL_TXPKTRDY);

	/* Update Data Toggle */
	td->toggle ^= 1;
	td->transaction_started = 1;

	return (1);			/* not complete */
}

static uint8_t
musbotg_xfer_do_fifo(struct usb_xfer *xfer)
{
	struct musbotg_softc *sc;
	struct musbotg_td *td;

	DPRINTFN(8, "\n");
	sc = MUSBOTG_BUS2SC(xfer->xroot->bus);

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
	musbotg_standard_done(xfer);

	return (0);			/* complete */
}

static void
musbotg_interrupt_poll(struct musbotg_softc *sc)
{
	struct usb_xfer *xfer;

repeat:
	TAILQ_FOREACH(xfer, &sc->sc_bus.intr_q.head, wait_entry) {
		if (!musbotg_xfer_do_fifo(xfer)) {
			/* queue has been modified */
			goto repeat;
		}
	}
}

void
musbotg_vbus_interrupt(struct musbotg_softc *sc, uint8_t is_on)
{
	DPRINTFN(4, "vbus = %u\n", is_on);

	USB_BUS_LOCK(&sc->sc_bus);
	if (is_on) {
		if (!sc->sc_flags.status_vbus) {
			sc->sc_flags.status_vbus = 1;

			/* complete root HUB interrupt endpoint */
			musbotg_root_intr(sc);
		}
	} else {
		if (sc->sc_flags.status_vbus) {
			sc->sc_flags.status_vbus = 0;
			sc->sc_flags.status_bus_reset = 0;
			sc->sc_flags.status_suspend = 0;
			sc->sc_flags.change_suspend = 0;
			sc->sc_flags.change_connect = 1;

			/* complete root HUB interrupt endpoint */
			musbotg_root_intr(sc);
		}
	}

	USB_BUS_UNLOCK(&sc->sc_bus);
}

void
musbotg_connect_interrupt(struct musbotg_softc *sc)
{
	USB_BUS_LOCK(&sc->sc_bus);
	sc->sc_flags.change_connect = 1;

	/* complete root HUB interrupt endpoint */
	musbotg_root_intr(sc);
	USB_BUS_UNLOCK(&sc->sc_bus);
}

void
musbotg_interrupt(struct musbotg_softc *sc,
    uint16_t rxstat, uint16_t txstat, uint8_t stat)
{
	uint16_t rx_status;
	uint16_t tx_status;
	uint8_t usb_status;
	uint8_t temp;
	uint8_t to = 2;

	USB_BUS_LOCK(&sc->sc_bus);

repeat:

	/* read all interrupt registers */
	usb_status = MUSB2_READ_1(sc, MUSB2_REG_INTUSB);

	/* read all FIFO interrupts */
	rx_status = MUSB2_READ_2(sc, MUSB2_REG_INTRX);
	tx_status = MUSB2_READ_2(sc, MUSB2_REG_INTTX);
	rx_status |= rxstat;
	tx_status |= txstat;
	usb_status |= stat;

	/* Clear platform flags after first time */
	rxstat = 0;
	txstat = 0;
	stat = 0;

	/* check for any bus state change interrupts */

	if (usb_status & (MUSB2_MASK_IRESET |
	    MUSB2_MASK_IRESUME | MUSB2_MASK_ISUSP | 
	    MUSB2_MASK_ICONN | MUSB2_MASK_IDISC |
	    MUSB2_MASK_IVBUSERR)) {

		DPRINTFN(4, "real bus interrupt 0x%08x\n", usb_status);

		if (usb_status & MUSB2_MASK_IRESET) {

			/* set correct state */
			sc->sc_flags.status_bus_reset = 1;
			sc->sc_flags.status_suspend = 0;
			sc->sc_flags.change_suspend = 0;
			sc->sc_flags.change_connect = 1;

			/* determine line speed */
			temp = MUSB2_READ_1(sc, MUSB2_REG_POWER);
			if (temp & MUSB2_MASK_HSMODE)
				sc->sc_flags.status_high_speed = 1;
			else
				sc->sc_flags.status_high_speed = 0;

			/*
			 * After reset all interrupts are on and we need to
			 * turn them off!
			 */
			temp = MUSB2_MASK_IRESET;
			/* disable resume interrupt */
			temp &= ~MUSB2_MASK_IRESUME;
			/* enable suspend interrupt */
			temp |= MUSB2_MASK_ISUSP;
			MUSB2_WRITE_1(sc, MUSB2_REG_INTUSBE, temp);
			/* disable TX and RX interrupts */
			MUSB2_WRITE_2(sc, MUSB2_REG_INTTXE, 0);
			MUSB2_WRITE_2(sc, MUSB2_REG_INTRXE, 0);
		}
		/*
	         * If RXRSM and RXSUSP is set at the same time we interpret
	         * that like RESUME. Resume is set when there is at least 3
	         * milliseconds of inactivity on the USB BUS.
	         */
		if (usb_status & MUSB2_MASK_IRESUME) {
			if (sc->sc_flags.status_suspend) {
				sc->sc_flags.status_suspend = 0;
				sc->sc_flags.change_suspend = 1;

				temp = MUSB2_READ_1(sc, MUSB2_REG_INTUSBE);
				/* disable resume interrupt */
				temp &= ~MUSB2_MASK_IRESUME;
				/* enable suspend interrupt */
				temp |= MUSB2_MASK_ISUSP;
				MUSB2_WRITE_1(sc, MUSB2_REG_INTUSBE, temp);
			}
		} else if (usb_status & MUSB2_MASK_ISUSP) {
			if (!sc->sc_flags.status_suspend) {
				sc->sc_flags.status_suspend = 1;
				sc->sc_flags.change_suspend = 1;

				temp = MUSB2_READ_1(sc, MUSB2_REG_INTUSBE);
				/* disable suspend interrupt */
				temp &= ~MUSB2_MASK_ISUSP;
				/* enable resume interrupt */
				temp |= MUSB2_MASK_IRESUME;
				MUSB2_WRITE_1(sc, MUSB2_REG_INTUSBE, temp);
			}
		}
		if (usb_status & 
		    (MUSB2_MASK_ICONN | MUSB2_MASK_IDISC))
			sc->sc_flags.change_connect = 1;

		/* 
		 * Host Mode: There is no IRESET so assume bus is 
		 * always in reset state once device is connected.
		 */
		if (sc->sc_mode == MUSB2_HOST_MODE) {
		    /* check for VBUS error in USB host mode */
		    if (usb_status & MUSB2_MASK_IVBUSERR) {
			temp = MUSB2_READ_1(sc, MUSB2_REG_DEVCTL);
			temp |= MUSB2_MASK_SESS;
			MUSB2_WRITE_1(sc, MUSB2_REG_DEVCTL, temp);
		    }
		    if (usb_status & MUSB2_MASK_ICONN)
			sc->sc_flags.status_bus_reset = 1;
		    if (usb_status & MUSB2_MASK_IDISC)
			sc->sc_flags.status_bus_reset = 0;
		}

		/* complete root HUB interrupt endpoint */
		musbotg_root_intr(sc);
	}
	/* check for any endpoint interrupts */

	if (rx_status || tx_status) {
		DPRINTFN(4, "real endpoint interrupt "
		    "rx=0x%04x, tx=0x%04x\n", rx_status, tx_status);
	}
	/* poll one time regardless of FIFO status */

	musbotg_interrupt_poll(sc);

	if (--to)
		goto repeat;

	USB_BUS_UNLOCK(&sc->sc_bus);
}

static void
musbotg_setup_standard_chain_sub(struct musbotg_std_temp *temp)
{
	struct musbotg_td *td;

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
	td->transaction_started = 0;
	td->did_stall = temp->did_stall;
	td->short_pkt = temp->short_pkt;
	td->alt_next = temp->setup_alt_next;
	td->channel = temp->channel;
	td->dev_addr = temp->dev_addr;
	td->haddr = temp->haddr;
	td->hport = temp->hport;
	td->transfer_type = temp->transfer_type;
}

static void
musbotg_setup_standard_chain(struct usb_xfer *xfer)
{
	struct musbotg_std_temp temp;
	struct musbotg_softc *sc;
	struct musbotg_td *td;
	uint32_t x;
	uint8_t ep_no;
	uint8_t xfer_type;
	enum usb_dev_speed speed;
	int tx;
	int dev_addr;

	DPRINTFN(8, "addr=%d endpt=%d sumlen=%d speed=%d\n",
	    xfer->address, UE_GET_ADDR(xfer->endpointno),
	    xfer->sumlen, usbd_get_speed(xfer->xroot->udev));

	sc = MUSBOTG_BUS2SC(xfer->xroot->bus);
	ep_no = (xfer->endpointno & UE_ADDR);

	temp.max_frame_size = xfer->max_frame_size;

	td = xfer->td_start[0];
	xfer->td_transfer_first = td;
	xfer->td_transfer_cache = td;

	/* setup temp */
	dev_addr = xfer->address;

	xfer_type = xfer->endpoint->edesc->bmAttributes & UE_XFERTYPE;

	temp.pc = NULL;
	temp.td = NULL;
	temp.td_next = xfer->td_start[0];
	temp.offset = 0;
	temp.setup_alt_next = xfer->flags_int.short_frames_ok ||
	    xfer->flags_int.isochronous_xfr;
	temp.did_stall = !xfer->flags_int.control_stall;
	temp.channel = -1;
	temp.dev_addr = dev_addr;
	temp.haddr = xfer->xroot->udev->hs_hub_addr;
	temp.hport = xfer->xroot->udev->hs_port_no;

	if (xfer->flags_int.usb_mode == USB_MODE_HOST) {
		speed =  usbd_get_speed(xfer->xroot->udev);

		switch (speed) {
			case USB_SPEED_LOW:
				temp.transfer_type = MUSB2_MASK_TI_SPEED_LO;
				break;
			case USB_SPEED_FULL:
				temp.transfer_type = MUSB2_MASK_TI_SPEED_FS;
				break;
			case USB_SPEED_HIGH:
				temp.transfer_type = MUSB2_MASK_TI_SPEED_HS;
				break;
			default:
				temp.transfer_type = 0;
				DPRINTFN(-1, "Invalid USB speed: %d\n", speed);
				break;
		}

		switch (xfer_type) {
			case UE_CONTROL:
				temp.transfer_type |= MUSB2_MASK_TI_PROTO_CTRL;
				break;
			case UE_ISOCHRONOUS:
				temp.transfer_type |= MUSB2_MASK_TI_PROTO_ISOC;
				break;
			case UE_BULK:
				temp.transfer_type |= MUSB2_MASK_TI_PROTO_BULK;
				break;
			case UE_INTERRUPT:
				temp.transfer_type |= MUSB2_MASK_TI_PROTO_INTR;
				break;
			default:
				DPRINTFN(-1, "Invalid USB transfer type: %d\n",
						xfer_type);
				break;
		}

		temp.transfer_type |= ep_no;
		td->toggle = xfer->endpoint->toggle_next;
	}

	/* check if we should prepend a setup message */

	if (xfer->flags_int.control_xfr) {
		if (xfer->flags_int.control_hdr) {

			if (xfer->flags_int.usb_mode == USB_MODE_DEVICE)
				temp.func = &musbotg_dev_ctrl_setup_rx;
			else
				temp.func = &musbotg_host_ctrl_setup_tx;

			temp.len = xfer->frlengths[0];
			temp.pc = xfer->frbuffers + 0;
			temp.short_pkt = temp.len ? 1 : 0;

			musbotg_setup_standard_chain_sub(&temp);
		}
		x = 1;
	} else {
		x = 0;
	}

	tx = 0;

	if (x != xfer->nframes) {
		if (xfer->endpointno & UE_DIR_IN)
		    	tx = 1;

		if (xfer->flags_int.usb_mode == USB_MODE_HOST) {
			tx = !tx;

			if (tx) {
				if (xfer->flags_int.control_xfr)
					temp.func = &musbotg_host_ctrl_data_tx;
				else
					temp.func = &musbotg_host_data_tx;
			} else {
				if (xfer->flags_int.control_xfr)
					temp.func = &musbotg_host_ctrl_data_rx;
				else
					temp.func = &musbotg_host_data_rx;
			}

		} else {
			if (tx) {
				if (xfer->flags_int.control_xfr)
					temp.func = &musbotg_dev_ctrl_data_tx;
				else
					temp.func = &musbotg_dev_data_tx;
			} else {
				if (xfer->flags_int.control_xfr)
					temp.func = &musbotg_dev_ctrl_data_rx;
				else
					temp.func = &musbotg_dev_data_rx;
			}
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

			if (xfer->flags_int.isochronous_xfr) {
				/* isochronous data transfer */
				/* don't force short */
				temp.short_pkt = 1;
			} else {
				/* regular data transfer */
				temp.short_pkt = (xfer->flags.force_short_xfer ? 0 : 1);
			}
		}

		musbotg_setup_standard_chain_sub(&temp);

		if (xfer->flags_int.isochronous_xfr) {
			temp.offset += temp.len;
		} else {
			/* get next Page Cache pointer */
			temp.pc = xfer->frbuffers + x;
		}
	}

	/* check for control transfer */
	if (xfer->flags_int.control_xfr) {

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
			if (sc->sc_mode == MUSB2_DEVICE_MODE)
				temp.func = &musbotg_dev_ctrl_status;
			else {
				if (xfer->endpointno & UE_DIR_IN)
					temp.func = musbotg_host_ctrl_status_tx;
				else
					temp.func = musbotg_host_ctrl_status_rx;
			}
			musbotg_setup_standard_chain_sub(&temp);
		}
	}
	/* must have at least one frame! */
	td = temp.td;
	xfer->td_transfer_last = td;
}

static void
musbotg_timeout(void *arg)
{
	struct usb_xfer *xfer = arg;

	DPRINTFN(1, "xfer=%p\n", xfer);

	USB_BUS_LOCK_ASSERT(xfer->xroot->bus, MA_OWNED);

	/* transfer is transferred */
	musbotg_device_done(xfer, USB_ERR_TIMEOUT);
}

static void
musbotg_ep_int_set(struct musbotg_softc *sc, int channel, int on)
{
	uint16_t temp;

	/*
	 * Only enable the endpoint interrupt when we are
	 * actually waiting for data, hence we are dealing
	 * with level triggered interrupts !
	 */
	DPRINTFN(1, "ep_no=%d, on=%d\n", channel, on);

	if (channel == -1)
		return;

	if (channel == 0) {
		temp = MUSB2_READ_2(sc, MUSB2_REG_INTTXE);
		if (on)
			temp |= MUSB2_MASK_EPINT(0);
		else
			temp &= ~MUSB2_MASK_EPINT(0);

		MUSB2_WRITE_2(sc, MUSB2_REG_INTTXE, temp);
	} else {
		temp = MUSB2_READ_2(sc, MUSB2_REG_INTRXE);
		if (on)
			temp |= MUSB2_MASK_EPINT(channel);
		else
			temp &= ~MUSB2_MASK_EPINT(channel);
		MUSB2_WRITE_2(sc, MUSB2_REG_INTRXE, temp);

		temp = MUSB2_READ_2(sc, MUSB2_REG_INTTXE);
		if (on)
			temp |= MUSB2_MASK_EPINT(channel);
		else
			temp &= ~MUSB2_MASK_EPINT(channel);
		MUSB2_WRITE_2(sc, MUSB2_REG_INTTXE, temp);
	}

	if (sc->sc_ep_int_set)
		sc->sc_ep_int_set(sc, channel, on);
}

static void
musbotg_start_standard_chain(struct usb_xfer *xfer)
{
	DPRINTFN(8, "\n");

	/* poll one time */
	if (musbotg_xfer_do_fifo(xfer)) {

		DPRINTFN(14, "enabled interrupts on endpoint\n");

		/* put transfer on interrupt queue */
		usbd_transfer_enqueue(&xfer->xroot->bus->intr_q, xfer);

		/* start timeout, if any */
		if (xfer->timeout != 0) {
			usbd_transfer_timeout_ms(xfer,
			    &musbotg_timeout, xfer->timeout);
		}
	}
}

static void
musbotg_root_intr(struct musbotg_softc *sc)
{
	DPRINTFN(8, "\n");

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	/* set port bit */
	sc->sc_hub_idata[0] = 0x02;	/* we only have one port */

	uhub_root_intr(&sc->sc_bus, sc->sc_hub_idata,
	    sizeof(sc->sc_hub_idata));
}

static usb_error_t
musbotg_standard_done_sub(struct usb_xfer *xfer)
{
	struct musbotg_td *td;
	uint32_t len;
	uint8_t error;

	DPRINTFN(8, "\n");

	td = xfer->td_transfer_cache;

	do {
		len = td->remainder;

		xfer->endpoint->toggle_next = td->toggle;

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
musbotg_standard_done(struct usb_xfer *xfer)
{
	usb_error_t err = 0;

	DPRINTFN(12, "xfer=%p endpoint=%p transfer done\n",
	    xfer, xfer->endpoint);

	/* reset scanner */

	xfer->td_transfer_cache = xfer->td_transfer_first;

	if (xfer->flags_int.control_xfr) {

		if (xfer->flags_int.control_hdr) {

			err = musbotg_standard_done_sub(xfer);
		}
		xfer->aframes = 1;

		if (xfer->td_transfer_cache == NULL) {
			goto done;
		}
	}
	while (xfer->aframes != xfer->nframes) {

		err = musbotg_standard_done_sub(xfer);
		xfer->aframes++;

		if (xfer->td_transfer_cache == NULL) {
			goto done;
		}
	}

	if (xfer->flags_int.control_xfr &&
	    !xfer->flags_int.control_act) {

		err = musbotg_standard_done_sub(xfer);
	}
done:
	musbotg_device_done(xfer, err);
}

/*------------------------------------------------------------------------*
 *	musbotg_device_done
 *
 * NOTE: this function can be called more than one time on the
 * same USB transfer!
 *------------------------------------------------------------------------*/
static void
musbotg_device_done(struct usb_xfer *xfer, usb_error_t error)
{
	struct musbotg_td *td;
	struct musbotg_softc *sc;

	USB_BUS_LOCK_ASSERT(xfer->xroot->bus, MA_OWNED);

	DPRINTFN(1, "xfer=%p, endpoint=%p, error=%d\n",
	    xfer, xfer->endpoint, error);

	DPRINTFN(14, "disabled interrupts on endpoint\n");

	sc = MUSBOTG_BUS2SC(xfer->xroot->bus);
	td = xfer->td_transfer_cache;

	if (td && (td->channel != -1))
		musbotg_channel_free(sc, td);

	/* dequeue transfer and start next transfer */
	usbd_transfer_done(xfer, error);
}

static void
musbotg_xfer_stall(struct usb_xfer *xfer)
{
	musbotg_device_done(xfer, USB_ERR_STALLED);
}

static void
musbotg_set_stall(struct usb_device *udev,
    struct usb_endpoint *ep, uint8_t *did_stall)
{
	struct musbotg_softc *sc;
	uint8_t ep_no;

	USB_BUS_LOCK_ASSERT(udev->bus, MA_OWNED);

	DPRINTFN(4, "endpoint=%p\n", ep);

	/* set FORCESTALL */
	sc = MUSBOTG_BUS2SC(udev->bus);

	ep_no = (ep->edesc->bEndpointAddress & UE_ADDR);

	/* select endpoint */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, ep_no);

	if (ep->edesc->bEndpointAddress & UE_DIR_IN) {
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
		    MUSB2_MASK_CSRL_TXSENDSTALL);
	} else {
		MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRL,
		    MUSB2_MASK_CSRL_RXSENDSTALL);
	}
}

static void
musbotg_clear_stall_sub(struct musbotg_softc *sc, uint16_t wMaxPacket,
    uint8_t ep_no, uint8_t ep_type, uint8_t ep_dir)
{
	uint16_t mps;
	uint16_t temp;
	uint8_t csr;

	if (ep_type == UE_CONTROL) {
		/* clearing stall is not needed */
		return;
	}
	/* select endpoint */
	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, ep_no);

	/* compute max frame size */
	mps = wMaxPacket & 0x7FF;
	switch ((wMaxPacket >> 11) & 3) {
	case 1:
		mps *= 2;
		break;
	case 2:
		mps *= 3;
		break;
	default:
		break;
	}

	if (ep_dir == UE_DIR_IN) {

		temp = 0;

		/* Configure endpoint */
		switch (ep_type) {
		case UE_INTERRUPT:
			MUSB2_WRITE_2(sc, MUSB2_REG_TXMAXP, wMaxPacket);
			MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRH,
			    MUSB2_MASK_CSRH_TXMODE | temp);
			break;
		case UE_ISOCHRONOUS:
			MUSB2_WRITE_2(sc, MUSB2_REG_TXMAXP, wMaxPacket);
			MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRH,
			    MUSB2_MASK_CSRH_TXMODE |
			    MUSB2_MASK_CSRH_TXISO | temp);
			break;
		case UE_BULK:
			MUSB2_WRITE_2(sc, MUSB2_REG_TXMAXP, wMaxPacket);
			MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRH,
			    MUSB2_MASK_CSRH_TXMODE | temp);
			break;
		default:
			break;
		}

		/* Need to flush twice in case of double bufring */
		csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
		if (csr & MUSB2_MASK_CSRL_TXFIFONEMPTY) {
			MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
			    MUSB2_MASK_CSRL_TXFFLUSH);
			csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
			if (csr & MUSB2_MASK_CSRL_TXFIFONEMPTY) {
				MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
				    MUSB2_MASK_CSRL_TXFFLUSH);
				csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
			}
		}
		/* reset data toggle */
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL,
		    MUSB2_MASK_CSRL_TXDT_CLR);
		MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, 0);
		csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);

		/* set double/single buffering */
		temp = MUSB2_READ_2(sc, MUSB2_REG_TXDBDIS);
		if (mps <= (sc->sc_hw_ep_profile[ep_no].
		    max_in_frame_size / 2)) {
			/* double buffer */
			temp &= ~(1 << ep_no);
		} else {
			/* single buffer */
			temp |= (1 << ep_no);
		}
		MUSB2_WRITE_2(sc, MUSB2_REG_TXDBDIS, temp);

		/* clear sent stall */
		if (csr & MUSB2_MASK_CSRL_TXSENTSTALL) {
			MUSB2_WRITE_1(sc, MUSB2_REG_TXCSRL, 0);
			csr = MUSB2_READ_1(sc, MUSB2_REG_TXCSRL);
		}
	} else {

		temp = 0;

		/* Configure endpoint */
		switch (ep_type) {
		case UE_INTERRUPT:
			MUSB2_WRITE_2(sc, MUSB2_REG_RXMAXP, wMaxPacket);
			MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRH,
			    MUSB2_MASK_CSRH_RXNYET | temp);
			break;
		case UE_ISOCHRONOUS:
			MUSB2_WRITE_2(sc, MUSB2_REG_RXMAXP, wMaxPacket);
			MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRH,
			    MUSB2_MASK_CSRH_RXNYET |
			    MUSB2_MASK_CSRH_RXISO | temp);
			break;
		case UE_BULK:
			MUSB2_WRITE_2(sc, MUSB2_REG_RXMAXP, wMaxPacket);
			MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRH, temp);
			break;
		default:
			break;
		}

		/* Need to flush twice in case of double bufring */
		csr = MUSB2_READ_1(sc, MUSB2_REG_RXCSRL);
		if (csr & MUSB2_MASK_CSRL_RXPKTRDY) {
			MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRL,
			    MUSB2_MASK_CSRL_RXFFLUSH);
			csr = MUSB2_READ_1(sc, MUSB2_REG_RXCSRL);
			if (csr & MUSB2_MASK_CSRL_RXPKTRDY) {
				MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRL,
				    MUSB2_MASK_CSRL_RXFFLUSH);
				csr = MUSB2_READ_1(sc, MUSB2_REG_RXCSRL);
			}
		}
		/* reset data toggle */
		MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRL,
		    MUSB2_MASK_CSRL_RXDT_CLR);
		MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRL, 0);
		csr = MUSB2_READ_1(sc, MUSB2_REG_RXCSRL);

		/* set double/single buffering */
		temp = MUSB2_READ_2(sc, MUSB2_REG_RXDBDIS);
		if (mps <= (sc->sc_hw_ep_profile[ep_no].
		    max_out_frame_size / 2)) {
			/* double buffer */
			temp &= ~(1 << ep_no);
		} else {
			/* single buffer */
			temp |= (1 << ep_no);
		}
		MUSB2_WRITE_2(sc, MUSB2_REG_RXDBDIS, temp);

		/* clear sent stall */
		if (csr & MUSB2_MASK_CSRL_RXSENTSTALL) {
			MUSB2_WRITE_1(sc, MUSB2_REG_RXCSRL, 0);
		}
	}
}

static void
musbotg_clear_stall(struct usb_device *udev, struct usb_endpoint *ep)
{
	struct musbotg_softc *sc;
	struct usb_endpoint_descriptor *ed;

	DPRINTFN(4, "endpoint=%p\n", ep);

	USB_BUS_LOCK_ASSERT(udev->bus, MA_OWNED);

	/* check mode */
	if (udev->flags.usb_mode != USB_MODE_DEVICE) {
		/* not supported */
		return;
	}
	/* get softc */
	sc = MUSBOTG_BUS2SC(udev->bus);

	/* get endpoint descriptor */
	ed = ep->edesc;

	/* reset endpoint */
	musbotg_clear_stall_sub(sc,
	    UGETW(ed->wMaxPacketSize),
	    (ed->bEndpointAddress & UE_ADDR),
	    (ed->bmAttributes & UE_XFERTYPE),
	    (ed->bEndpointAddress & (UE_DIR_IN | UE_DIR_OUT)));
}

usb_error_t
musbotg_init(struct musbotg_softc *sc)
{
	const struct musb_otg_ep_cfg *cfg;
	struct usb_hw_ep_profile *pf;
	int i;
	uint16_t offset;
	uint8_t nrx;
	uint8_t ntx;
	uint8_t temp;
	uint8_t fsize;
	uint8_t frx;
	uint8_t ftx;
	uint8_t dynfifo;

	DPRINTFN(1, "start\n");

	/* set up the bus structure */
	sc->sc_bus.usbrev = USB_REV_2_0;
	sc->sc_bus.methods = &musbotg_bus_methods;

	/* Set a default endpoint configuration */
	if (sc->sc_ep_cfg == NULL)
		sc->sc_ep_cfg = musbotg_ep_default;

	USB_BUS_LOCK(&sc->sc_bus);

	/* turn on clocks */

	if (sc->sc_clocks_on) {
		(sc->sc_clocks_on) (sc->sc_clocks_arg);
	}

	/* wait a little for things to stabilise */
	usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 1000);

	/* disable all interrupts */

	temp = MUSB2_READ_1(sc, MUSB2_REG_DEVCTL);
	DPRINTF("pre-DEVCTL=0x%02x\n", temp);

	MUSB2_WRITE_1(sc, MUSB2_REG_INTUSBE, 0);
	MUSB2_WRITE_2(sc, MUSB2_REG_INTTXE, 0);
	MUSB2_WRITE_2(sc, MUSB2_REG_INTRXE, 0);

	/* disable pullup */

	musbotg_pull_common(sc, 0);

	/* wait a little bit (10ms) */
	usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 100);


	/* disable double packet buffering */
	MUSB2_WRITE_2(sc, MUSB2_REG_RXDBDIS, 0xFFFF);
	MUSB2_WRITE_2(sc, MUSB2_REG_TXDBDIS, 0xFFFF);

	/* enable HighSpeed and ISO Update flags */

	MUSB2_WRITE_1(sc, MUSB2_REG_POWER,
	    MUSB2_MASK_HSENAB | MUSB2_MASK_ISOUPD);

	if (sc->sc_mode == MUSB2_DEVICE_MODE) {
		/* clear Session bit, if set */
		temp = MUSB2_READ_1(sc, MUSB2_REG_DEVCTL);
		temp &= ~MUSB2_MASK_SESS;
		MUSB2_WRITE_1(sc, MUSB2_REG_DEVCTL, temp);
	} else {
		/* Enter session for Host mode */
		temp = MUSB2_READ_1(sc, MUSB2_REG_DEVCTL);
		temp |= MUSB2_MASK_SESS;
		MUSB2_WRITE_1(sc, MUSB2_REG_DEVCTL, temp);
	}

	/* wait a little for things to stabilise */
	usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 10);

	DPRINTF("DEVCTL=0x%02x\n", temp);

	/* disable testmode */

	MUSB2_WRITE_1(sc, MUSB2_REG_TESTMODE, 0);

	/* set default value */

	MUSB2_WRITE_1(sc, MUSB2_REG_MISC, 0);

	/* select endpoint index 0 */

	MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, 0);

	if (sc->sc_ep_max == 0) {
		/* read out number of endpoints */

		nrx =
		    (MUSB2_READ_1(sc, MUSB2_REG_EPINFO) / 16);

		ntx =
		    (MUSB2_READ_1(sc, MUSB2_REG_EPINFO) % 16);

		sc->sc_ep_max = (nrx > ntx) ? nrx : ntx;
	} else {
		nrx = ntx = sc->sc_ep_max;
	}

	/* these numbers exclude the control endpoint */

	DPRINTFN(2, "RX/TX endpoints: %u/%u\n", nrx, ntx);

	if (sc->sc_ep_max == 0) {
		DPRINTFN(2, "ERROR: Looks like the clocks are off!\n");
	}
	/* read out configuration data */

	sc->sc_conf_data = MUSB2_READ_1(sc, MUSB2_REG_CONFDATA);

	DPRINTFN(2, "Config Data: 0x%02x\n",
	    sc->sc_conf_data);

	dynfifo = (sc->sc_conf_data & MUSB2_MASK_CD_DYNFIFOSZ) ? 1 : 0;

	if (dynfifo) {
		device_printf(sc->sc_bus.bdev, "Dynamic FIFO sizing detected, "
		    "assuming 16Kbytes of FIFO RAM\n");
	}

	DPRINTFN(2, "HW version: 0x%04x\n",
	    MUSB2_READ_1(sc, MUSB2_REG_HWVERS));

	/* initialise endpoint profiles */

	offset = 0;

	for (temp = 1; temp <= sc->sc_ep_max; temp++) {
		pf = sc->sc_hw_ep_profile + temp;

		/* select endpoint */
		MUSB2_WRITE_1(sc, MUSB2_REG_EPINDEX, temp);

		fsize = MUSB2_READ_1(sc, MUSB2_REG_FSIZE);
		frx = (fsize & MUSB2_MASK_RX_FSIZE) / 16;
		ftx = (fsize & MUSB2_MASK_TX_FSIZE);

		DPRINTF("Endpoint %u FIFO size: IN=%u, OUT=%u, DYN=%d\n",
		    temp, ftx, frx, dynfifo);

		if (dynfifo) {
			if (frx && (temp <= nrx)) {
				for (i = 0; sc->sc_ep_cfg[i].ep_end >= 0; i++) {
					cfg = &sc->sc_ep_cfg[i];
					if (temp <= cfg->ep_end) {
						frx = cfg->ep_fifosz_shift;
						MUSB2_WRITE_1(sc,
						    MUSB2_REG_RXFIFOSZ,
						    cfg->ep_fifosz_reg);
						break;
					}
				}

				MUSB2_WRITE_2(sc, MUSB2_REG_RXFIFOADD,
				    offset >> 3);

				offset += (1 << frx);
			}
			if (ftx && (temp <= ntx)) {
				for (i = 0; sc->sc_ep_cfg[i].ep_end >= 0; i++) {
					cfg = &sc->sc_ep_cfg[i];
					if (temp <= cfg->ep_end) {
						ftx = cfg->ep_fifosz_shift;
						MUSB2_WRITE_1(sc,
						    MUSB2_REG_TXFIFOSZ,
						    cfg->ep_fifosz_reg);
						break;
					}
				}

				MUSB2_WRITE_2(sc, MUSB2_REG_TXFIFOADD,
				    offset >> 3);

				offset += (1 << ftx);
			}
		}

		if (frx && ftx && (temp <= nrx) && (temp <= ntx)) {
			pf->max_in_frame_size = 1 << ftx;
			pf->max_out_frame_size = 1 << frx;
			pf->is_simplex = 0;	/* duplex */
			pf->support_multi_buffer = 1;
			pf->support_bulk = 1;
			pf->support_interrupt = 1;
			pf->support_isochronous = 1;
			pf->support_in = 1;
			pf->support_out = 1;
		} else if (frx && (temp <= nrx)) {
			pf->max_out_frame_size = 1 << frx;
			pf->max_in_frame_size = 0;
			pf->is_simplex = 1;	/* simplex */
			pf->support_multi_buffer = 1;
			pf->support_bulk = 1;
			pf->support_interrupt = 1;
			pf->support_isochronous = 1;
			pf->support_out = 1;
		} else if (ftx && (temp <= ntx)) {
			pf->max_in_frame_size = 1 << ftx;
			pf->max_out_frame_size = 0;
			pf->is_simplex = 1;	/* simplex */
			pf->support_multi_buffer = 1;
			pf->support_bulk = 1;
			pf->support_interrupt = 1;
			pf->support_isochronous = 1;
			pf->support_in = 1;
		}
	}

	DPRINTFN(2, "Dynamic FIFO size = %d bytes\n", offset);

	/* turn on default interrupts */

	if (sc->sc_mode == MUSB2_HOST_MODE)
		MUSB2_WRITE_1(sc, MUSB2_REG_INTUSBE, 0xff);
	else
		MUSB2_WRITE_1(sc, MUSB2_REG_INTUSBE,
		    MUSB2_MASK_IRESET);

	musbotg_clocks_off(sc);

	USB_BUS_UNLOCK(&sc->sc_bus);

	/* catch any lost interrupts */

	musbotg_do_poll(&sc->sc_bus);

	return (0);			/* success */
}

void
musbotg_uninit(struct musbotg_softc *sc)
{
	USB_BUS_LOCK(&sc->sc_bus);

	/* disable all interrupts */
	MUSB2_WRITE_1(sc, MUSB2_REG_INTUSBE, 0);
	MUSB2_WRITE_2(sc, MUSB2_REG_INTTXE, 0);
	MUSB2_WRITE_2(sc, MUSB2_REG_INTRXE, 0);

	sc->sc_flags.port_powered = 0;
	sc->sc_flags.status_vbus = 0;
	sc->sc_flags.status_bus_reset = 0;
	sc->sc_flags.status_suspend = 0;
	sc->sc_flags.change_suspend = 0;
	sc->sc_flags.change_connect = 1;

	musbotg_pull_down(sc);
	musbotg_clocks_off(sc);
	USB_BUS_UNLOCK(&sc->sc_bus);
}

static void
musbotg_do_poll(struct usb_bus *bus)
{
	struct musbotg_softc *sc = MUSBOTG_BUS2SC(bus);

	USB_BUS_LOCK(&sc->sc_bus);
	musbotg_interrupt_poll(sc);
	USB_BUS_UNLOCK(&sc->sc_bus);
}

/*------------------------------------------------------------------------*
 * musbotg bulk support
 *------------------------------------------------------------------------*/
static void
musbotg_device_bulk_open(struct usb_xfer *xfer)
{
	return;
}

static void
musbotg_device_bulk_close(struct usb_xfer *xfer)
{
	musbotg_device_done(xfer, USB_ERR_CANCELLED);
}

static void
musbotg_device_bulk_enter(struct usb_xfer *xfer)
{
	return;
}

static void
musbotg_device_bulk_start(struct usb_xfer *xfer)
{
	/* setup TDs */
	musbotg_setup_standard_chain(xfer);
	musbotg_start_standard_chain(xfer);
}

static const struct usb_pipe_methods musbotg_device_bulk_methods =
{
	.open = musbotg_device_bulk_open,
	.close = musbotg_device_bulk_close,
	.enter = musbotg_device_bulk_enter,
	.start = musbotg_device_bulk_start,
};

/*------------------------------------------------------------------------*
 * musbotg control support
 *------------------------------------------------------------------------*/
static void
musbotg_device_ctrl_open(struct usb_xfer *xfer)
{
	return;
}

static void
musbotg_device_ctrl_close(struct usb_xfer *xfer)
{
	musbotg_device_done(xfer, USB_ERR_CANCELLED);
}

static void
musbotg_device_ctrl_enter(struct usb_xfer *xfer)
{
	return;
}

static void
musbotg_device_ctrl_start(struct usb_xfer *xfer)
{
	/* setup TDs */
	musbotg_setup_standard_chain(xfer);
	musbotg_start_standard_chain(xfer);
}

static const struct usb_pipe_methods musbotg_device_ctrl_methods =
{
	.open = musbotg_device_ctrl_open,
	.close = musbotg_device_ctrl_close,
	.enter = musbotg_device_ctrl_enter,
	.start = musbotg_device_ctrl_start,
};

/*------------------------------------------------------------------------*
 * musbotg interrupt support
 *------------------------------------------------------------------------*/
static void
musbotg_device_intr_open(struct usb_xfer *xfer)
{
	return;
}

static void
musbotg_device_intr_close(struct usb_xfer *xfer)
{
	musbotg_device_done(xfer, USB_ERR_CANCELLED);
}

static void
musbotg_device_intr_enter(struct usb_xfer *xfer)
{
	return;
}

static void
musbotg_device_intr_start(struct usb_xfer *xfer)
{
	/* setup TDs */
	musbotg_setup_standard_chain(xfer);
	musbotg_start_standard_chain(xfer);
}

static const struct usb_pipe_methods musbotg_device_intr_methods =
{
	.open = musbotg_device_intr_open,
	.close = musbotg_device_intr_close,
	.enter = musbotg_device_intr_enter,
	.start = musbotg_device_intr_start,
};

/*------------------------------------------------------------------------*
 * musbotg full speed isochronous support
 *------------------------------------------------------------------------*/
static void
musbotg_device_isoc_open(struct usb_xfer *xfer)
{
	return;
}

static void
musbotg_device_isoc_close(struct usb_xfer *xfer)
{
	musbotg_device_done(xfer, USB_ERR_CANCELLED);
}

static void
musbotg_device_isoc_enter(struct usb_xfer *xfer)
{
	struct musbotg_softc *sc = MUSBOTG_BUS2SC(xfer->xroot->bus);
	uint32_t temp;
	uint32_t nframes;
	uint32_t fs_frames;

	DPRINTFN(5, "xfer=%p next=%d nframes=%d\n",
	    xfer, xfer->endpoint->isoc_next, xfer->nframes);

	/* get the current frame index */

	nframes = MUSB2_READ_2(sc, MUSB2_REG_FRAME);

	/*
	 * check if the frame index is within the window where the frames
	 * will be inserted
	 */
	temp = (nframes - xfer->endpoint->isoc_next) & MUSB2_MASK_FRAME;

	if (usbd_get_speed(xfer->xroot->udev) == USB_SPEED_HIGH) {
		fs_frames = (xfer->nframes + 7) / 8;
	} else {
		fs_frames = xfer->nframes;
	}

	if ((xfer->endpoint->is_synced == 0) ||
	    (temp < fs_frames)) {
		/*
		 * If there is data underflow or the pipe queue is
		 * empty we schedule the transfer a few frames ahead
		 * of the current frame position. Else two isochronous
		 * transfers might overlap.
		 */
		xfer->endpoint->isoc_next = (nframes + 3) & MUSB2_MASK_FRAME;
		xfer->endpoint->is_synced = 1;
		DPRINTFN(2, "start next=%d\n", xfer->endpoint->isoc_next);
	}
	/*
	 * compute how many milliseconds the insertion is ahead of the
	 * current frame position:
	 */
	temp = (xfer->endpoint->isoc_next - nframes) & MUSB2_MASK_FRAME;

	/*
	 * pre-compute when the isochronous transfer will be finished:
	 */
	xfer->isoc_time_complete =
	    usb_isoc_time_expand(&sc->sc_bus, nframes) + temp +
	    fs_frames;

	/* compute frame number for next insertion */
	xfer->endpoint->isoc_next += fs_frames;

	/* setup TDs */
	musbotg_setup_standard_chain(xfer);
}

static void
musbotg_device_isoc_start(struct usb_xfer *xfer)
{
	/* start TD chain */
	musbotg_start_standard_chain(xfer);
}

static const struct usb_pipe_methods musbotg_device_isoc_methods =
{
	.open = musbotg_device_isoc_open,
	.close = musbotg_device_isoc_close,
	.enter = musbotg_device_isoc_enter,
	.start = musbotg_device_isoc_start,
};

/*------------------------------------------------------------------------*
 * musbotg root control support
 *------------------------------------------------------------------------*
 * Simulate a hardware HUB by handling all the necessary requests.
 *------------------------------------------------------------------------*/

static const struct usb_device_descriptor musbotg_devd = {
	.bLength = sizeof(struct usb_device_descriptor),
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

static const struct usb_device_qualifier musbotg_odevd = {
	.bLength = sizeof(struct usb_device_qualifier),
	.bDescriptorType = UDESC_DEVICE_QUALIFIER,
	.bcdUSB = {0x00, 0x02},
	.bDeviceClass = UDCLASS_HUB,
	.bDeviceSubClass = UDSUBCLASS_HUB,
	.bDeviceProtocol = UDPROTO_FSHUB,
	.bMaxPacketSize0 = 0,
	.bNumConfigurations = 0,
};

static const struct musbotg_config_desc musbotg_confd = {
	.confd = {
		.bLength = sizeof(struct usb_config_descriptor),
		.bDescriptorType = UDESC_CONFIG,
		.wTotalLength[0] = sizeof(musbotg_confd),
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
		.bEndpointAddress = (UE_DIR_IN | MUSBOTG_INTR_ENDPT),
		.bmAttributes = UE_INTERRUPT,
		.wMaxPacketSize[0] = 8,
		.bInterval = 255,
	},
};

#define	HSETW(ptr, val) ptr = { (uint8_t)(val), (uint8_t)((val) >> 8) }

static const struct usb_hub_descriptor_min musbotg_hubd = {
	.bDescLength = sizeof(musbotg_hubd),
	.bDescriptorType = UDESC_HUB,
	.bNbrPorts = 1,
	HSETW(.wHubCharacteristics, (UHD_PWR_NO_SWITCH | UHD_OC_INDIVIDUAL)),
	.bPwrOn2PwrGood = 50,
	.bHubContrCurrent = 0,
	.DeviceRemovable = {0},		/* port is removable */
};

#define	STRING_VENDOR \
  "M\0e\0n\0t\0o\0r\0 \0G\0r\0a\0p\0h\0i\0c\0s"

#define	STRING_PRODUCT \
  "O\0T\0G\0 \0R\0o\0o\0t\0 \0H\0U\0B"

USB_MAKE_STRING_DESC(STRING_VENDOR, musbotg_vendor);
USB_MAKE_STRING_DESC(STRING_PRODUCT, musbotg_product);

static usb_error_t
musbotg_roothub_exec(struct usb_device *udev,
    struct usb_device_request *req, const void **pptr, uint16_t *plength)
{
	struct musbotg_softc *sc = MUSBOTG_BUS2SC(udev->bus);
	const void *ptr;
	uint16_t len;
	uint16_t value;
	uint16_t index;
	uint8_t reg;
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
		len = sizeof(musbotg_devd);
		ptr = (const void *)&musbotg_devd;
		goto tr_valid;
	case UDESC_DEVICE_QUALIFIER:
		if (value & 0xff) {
			goto tr_stalled;
		}
		len = sizeof(musbotg_odevd);
		ptr = (const void *)&musbotg_odevd;
		goto tr_valid;
	case UDESC_CONFIG:
		if (value & 0xff) {
			goto tr_stalled;
		}
		len = sizeof(musbotg_confd);
		ptr = (const void *)&musbotg_confd;
		goto tr_valid;
	case UDESC_STRING:
		switch (value & 0xff) {
		case 0:		/* Language table */
			len = sizeof(usb_string_lang_en);
			ptr = (const void *)&usb_string_lang_en;
			goto tr_valid;

		case 1:		/* Vendor */
			len = sizeof(musbotg_vendor);
			ptr = (const void *)&musbotg_vendor;
			goto tr_valid;

		case 2:		/* Product */
			len = sizeof(musbotg_product);
			ptr = (const void *)&musbotg_product;
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
	DPRINTFN(8, "UR_CLEAR_PORT_FEATURE on port %d\n", index);

	switch (value) {
	case UHF_PORT_SUSPEND:
		if (sc->sc_mode == MUSB2_HOST_MODE)
			musbotg_wakeup_host(sc);
		else
			musbotg_wakeup_peer(sc);
		break;

	case UHF_PORT_ENABLE:
		sc->sc_flags.port_enabled = 0;
		break;

	case UHF_C_PORT_ENABLE:
		sc->sc_flags.change_enabled = 0;
		break;

	case UHF_C_PORT_OVER_CURRENT:
		sc->sc_flags.change_over_current = 0;
		break;

	case UHF_C_PORT_RESET:
		sc->sc_flags.change_reset = 0;
		break;

	case UHF_PORT_TEST:
	case UHF_PORT_INDICATOR:
		/* nops */
		break;

	case UHF_PORT_POWER:
		sc->sc_flags.port_powered = 0;
		musbotg_pull_down(sc);
		musbotg_clocks_off(sc);
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
	DPRINTFN(8, "UR_SET_PORT_FEATURE\n");

	switch (value) {
	case UHF_PORT_ENABLE:
		sc->sc_flags.port_enabled = 1;
		break;
	case UHF_PORT_SUSPEND:
		if (sc->sc_mode == MUSB2_HOST_MODE)
			musbotg_suspend_host(sc);
		break;

	case UHF_PORT_RESET:
		if (sc->sc_mode == MUSB2_HOST_MODE) {
			reg = MUSB2_READ_1(sc, MUSB2_REG_POWER);
			reg |= MUSB2_MASK_RESET;
			MUSB2_WRITE_1(sc, MUSB2_REG_POWER, reg);

			/* Wait for 20 msec */
			usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 5);

			reg = MUSB2_READ_1(sc, MUSB2_REG_POWER);
			reg &= ~MUSB2_MASK_RESET;
			MUSB2_WRITE_1(sc, MUSB2_REG_POWER, reg);

			/* determine line speed */
			reg = MUSB2_READ_1(sc, MUSB2_REG_POWER);
			if (reg & MUSB2_MASK_HSMODE)
				sc->sc_flags.status_high_speed = 1;
			else
				sc->sc_flags.status_high_speed = 0;

			sc->sc_flags.change_reset = 1;
		} else
			err = USB_ERR_IOERROR;
		break;

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

	DPRINTFN(8, "UR_GET_PORT_STATUS\n");

	if (index != 1) {
		goto tr_stalled;
	}
	if (sc->sc_flags.status_vbus) {
		musbotg_clocks_on(sc);
		musbotg_pull_up(sc);
	} else {
		musbotg_pull_down(sc);
		musbotg_clocks_off(sc);
	}

	/* Select Device Side Mode */
	if (sc->sc_mode == MUSB2_DEVICE_MODE)
		value = UPS_PORT_MODE_DEVICE;
	else
		value = 0;

	if (sc->sc_flags.status_high_speed) {
		value |= UPS_HIGH_SPEED;
	}
	if (sc->sc_flags.port_powered) {
		value |= UPS_PORT_POWER;
	}
	if (sc->sc_flags.port_enabled) {
		value |= UPS_PORT_ENABLED;
	}

	if (sc->sc_flags.port_over_current)
		value |= UPS_OVERCURRENT_INDICATOR;

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

		if (sc->sc_mode == MUSB2_DEVICE_MODE) {
			if (sc->sc_flags.status_vbus &&
			    sc->sc_flags.status_bus_reset) {
				/* reset EP0 state */
				sc->sc_ep0_busy = 0;
				sc->sc_ep0_cmd = 0;
			}
		}
	}
	if (sc->sc_flags.change_suspend)
		value |= UPS_C_SUSPEND;
	if (sc->sc_flags.change_reset)
		value |= UPS_C_PORT_RESET;
	if (sc->sc_flags.change_over_current)
		value |= UPS_C_OVERCURRENT_INDICATOR;

	USETW(sc->sc_hub_temp.ps.wPortChange, value);
	len = sizeof(sc->sc_hub_temp.ps);
	goto tr_valid;

tr_handle_get_class_descriptor:
	if (value & 0xFF) {
		goto tr_stalled;
	}
	ptr = (const void *)&musbotg_hubd;
	len = sizeof(musbotg_hubd);
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
musbotg_xfer_setup(struct usb_setup_params *parm)
{
	struct musbotg_softc *sc;
	struct usb_xfer *xfer;
	void *last_obj;
	uint32_t ntd;
	uint32_t n;
	uint8_t ep_no;

	sc = MUSBOTG_BUS2SC(parm->udev->bus);
	xfer = parm->curr_xfer;

	/*
	 * NOTE: This driver does not use any of the parameters that
	 * are computed from the following values. Just set some
	 * reasonable dummies:
	 */
	parm->hc_max_packet_size = 0x400;
	parm->hc_max_frame_size = 0xc00;

	if ((parm->methods == &musbotg_device_isoc_methods) ||
	    (parm->methods == &musbotg_device_intr_methods))
		parm->hc_max_packet_count = 3;
	else
		parm->hc_max_packet_count = 1;

	usbd_transfer_setup_sub(parm);

	/*
	 * compute maximum number of TDs
	 */
	if (parm->methods == &musbotg_device_ctrl_methods) {

		ntd = xfer->nframes + 1 /* STATUS */ + 1 /* SYNC */ ;

	} else if (parm->methods == &musbotg_device_bulk_methods) {

		ntd = xfer->nframes + 1 /* SYNC */ ;

	} else if (parm->methods == &musbotg_device_intr_methods) {

		ntd = xfer->nframes + 1 /* SYNC */ ;

	} else if (parm->methods == &musbotg_device_isoc_methods) {

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

	ep_no = xfer->endpointno & UE_ADDR;

	/*
	 * Check for a valid endpoint profile in USB device mode:
	 */
	if (xfer->flags_int.usb_mode == USB_MODE_DEVICE) {
		const struct usb_hw_ep_profile *pf;

		musbotg_get_hw_ep_profile(parm->udev, &pf, ep_no);

		if (pf == NULL) {
			/* should not happen */
			parm->err = USB_ERR_INVAL;
			return;
		}
	}

	/* align data */
	parm->size[0] += ((-parm->size[0]) & (USB_HOST_ALIGN - 1));

	for (n = 0; n != ntd; n++) {

		struct musbotg_td *td;

		if (parm->buf) {

			td = USB_ADD_BYTES(parm->buf, parm->size[0]);

			/* init TD */
			td->max_frame_size = xfer->max_frame_size;
			td->reg_max_packet = xfer->max_packet_size |
			    ((xfer->max_packet_count - 1) << 11);
			td->ep_no = ep_no;
			td->obj_next = last_obj;

			last_obj = td;
		}
		parm->size[0] += sizeof(*td);
	}

	xfer->td_start[0] = last_obj;
}

static void
musbotg_xfer_unsetup(struct usb_xfer *xfer)
{
	return;
}

static void
musbotg_get_dma_delay(struct usb_device *udev, uint32_t *pus)
{
	struct musbotg_softc *sc = MUSBOTG_BUS2SC(udev->bus);

	if (sc->sc_mode == MUSB2_HOST_MODE)
	        *pus = 2000;                   /* microseconds */
	else
		*pus = 0;
}

static void
musbotg_ep_init(struct usb_device *udev, struct usb_endpoint_descriptor *edesc,
    struct usb_endpoint *ep)
{
	struct musbotg_softc *sc = MUSBOTG_BUS2SC(udev->bus);

	DPRINTFN(2, "endpoint=%p, addr=%d, endpt=%d, mode=%d (%d)\n",
	    ep, udev->address,
	    edesc->bEndpointAddress, udev->flags.usb_mode,
	    sc->sc_rt_addr);

	if (udev->device_index != sc->sc_rt_addr) {
		switch (edesc->bmAttributes & UE_XFERTYPE) {
		case UE_CONTROL:
			ep->methods = &musbotg_device_ctrl_methods;
			break;
		case UE_INTERRUPT:
			ep->methods = &musbotg_device_intr_methods;
			break;
		case UE_ISOCHRONOUS:
			ep->methods = &musbotg_device_isoc_methods;
			break;
		case UE_BULK:
			ep->methods = &musbotg_device_bulk_methods;
			break;
		default:
			/* do nothing */
			break;
		}
	}
}

static void
musbotg_set_hw_power_sleep(struct usb_bus *bus, uint32_t state)
{
	struct musbotg_softc *sc = MUSBOTG_BUS2SC(bus);

	switch (state) {
	case USB_HW_POWER_SUSPEND:
		musbotg_uninit(sc);
		break;
	case USB_HW_POWER_SHUTDOWN:
		musbotg_uninit(sc);
		break;
	case USB_HW_POWER_RESUME:
		musbotg_init(sc);
		break;
	default:
		break;
	}
}

static const struct usb_bus_methods musbotg_bus_methods =
{
	.endpoint_init = &musbotg_ep_init,
	.get_dma_delay = &musbotg_get_dma_delay,
	.xfer_setup = &musbotg_xfer_setup,
	.xfer_unsetup = &musbotg_xfer_unsetup,
	.get_hw_ep_profile = &musbotg_get_hw_ep_profile,
	.xfer_stall = &musbotg_xfer_stall,
	.set_stall = &musbotg_set_stall,
	.clear_stall = &musbotg_clear_stall,
	.roothub_exec = &musbotg_roothub_exec,
	.xfer_poll = &musbotg_do_poll,
	.set_hw_power_sleep = &musbotg_set_hw_power_sleep,
};
