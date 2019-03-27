/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
 * Copyright (c) 1998 The NetBSD Foundation, Inc. All rights reserved.
 * Copyright (c) 1998 Lennart Augustsson. All rights reserved.
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
 * USB Universal Host Controller driver.
 * Handles e.g. PIIX3 and PIIX4.
 *
 * UHCI spec: http://developer.intel.com/design/USB/UHCI11D.htm
 * USB spec:  http://www.usb.org/developers/docs/usbspec.zip
 * PIIXn spec: ftp://download.intel.com/design/intarch/datashts/29055002.pdf
 *             ftp://download.intel.com/design/intarch/datashts/29056201.pdf
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

#define	USB_DEBUG_VAR uhcidebug

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

#include <dev/usb/controller/uhci.h>
#include <dev/usb/controller/uhcireg.h>

#define	alt_next next
#define	UHCI_BUS2SC(bus) \
   ((uhci_softc_t *)(((uint8_t *)(bus)) - \
    ((uint8_t *)&(((uhci_softc_t *)0)->sc_bus))))

#ifdef USB_DEBUG
static int uhcidebug = 0;
static int uhcinoloop = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, uhci, CTLFLAG_RW, 0, "USB uhci");
SYSCTL_INT(_hw_usb_uhci, OID_AUTO, debug, CTLFLAG_RWTUN,
    &uhcidebug, 0, "uhci debug level");
SYSCTL_INT(_hw_usb_uhci, OID_AUTO, loop, CTLFLAG_RWTUN,
    &uhcinoloop, 0, "uhci noloop");

static void uhci_dumpregs(uhci_softc_t *sc);
static void uhci_dump_tds(uhci_td_t *td);

#endif

#define	UBARR(sc) bus_space_barrier((sc)->sc_io_tag, (sc)->sc_io_hdl, 0, (sc)->sc_io_size, \
			BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE)
#define	UWRITE1(sc, r, x) \
 do { UBARR(sc); bus_space_write_1((sc)->sc_io_tag, (sc)->sc_io_hdl, (r), (x)); \
 } while (/*CONSTCOND*/0)
#define	UWRITE2(sc, r, x) \
 do { UBARR(sc); bus_space_write_2((sc)->sc_io_tag, (sc)->sc_io_hdl, (r), (x)); \
 } while (/*CONSTCOND*/0)
#define	UWRITE4(sc, r, x) \
 do { UBARR(sc); bus_space_write_4((sc)->sc_io_tag, (sc)->sc_io_hdl, (r), (x)); \
 } while (/*CONSTCOND*/0)
#define	UREAD1(sc, r) (UBARR(sc), bus_space_read_1((sc)->sc_io_tag, (sc)->sc_io_hdl, (r)))
#define	UREAD2(sc, r) (UBARR(sc), bus_space_read_2((sc)->sc_io_tag, (sc)->sc_io_hdl, (r)))
#define	UREAD4(sc, r) (UBARR(sc), bus_space_read_4((sc)->sc_io_tag, (sc)->sc_io_hdl, (r)))

#define	UHCICMD(sc, cmd) UWRITE2(sc, UHCI_CMD, cmd)
#define	UHCISTS(sc) UREAD2(sc, UHCI_STS)

#define	UHCI_RESET_TIMEOUT 100		/* ms, reset timeout */

#define	UHCI_INTR_ENDPT 1

struct uhci_mem_layout {

	struct usb_page_search buf_res;
	struct usb_page_search fix_res;

	struct usb_page_cache *buf_pc;
	struct usb_page_cache *fix_pc;

	uint32_t buf_offset;

	uint16_t max_frame_size;
};

struct uhci_std_temp {

	struct uhci_mem_layout ml;
	uhci_td_t *td;
	uhci_td_t *td_next;
	uint32_t average;
	uint32_t td_status;
	uint32_t td_token;
	uint32_t len;
	uint16_t max_frame_size;
	uint8_t	shortpkt;
	uint8_t	setup_alt_next;
	uint8_t	last_frame;
};

static const struct usb_bus_methods uhci_bus_methods;
static const struct usb_pipe_methods uhci_device_bulk_methods;
static const struct usb_pipe_methods uhci_device_ctrl_methods;
static const struct usb_pipe_methods uhci_device_intr_methods;
static const struct usb_pipe_methods uhci_device_isoc_methods;

static uint8_t	uhci_restart(uhci_softc_t *sc);
static void	uhci_do_poll(struct usb_bus *);
static void	uhci_device_done(struct usb_xfer *, usb_error_t);
static void	uhci_transfer_intr_enqueue(struct usb_xfer *);
static void	uhci_timeout(void *);
static uint8_t	uhci_check_transfer(struct usb_xfer *);
static void	uhci_root_intr(uhci_softc_t *sc);

void
uhci_iterate_hw_softc(struct usb_bus *bus, usb_bus_mem_sub_cb_t *cb)
{
	struct uhci_softc *sc = UHCI_BUS2SC(bus);
	uint32_t i;

	cb(bus, &sc->sc_hw.pframes_pc, &sc->sc_hw.pframes_pg,
	    sizeof(uint32_t) * UHCI_FRAMELIST_COUNT, UHCI_FRAMELIST_ALIGN);

	cb(bus, &sc->sc_hw.ls_ctl_start_pc, &sc->sc_hw.ls_ctl_start_pg,
	    sizeof(uhci_qh_t), UHCI_QH_ALIGN);

	cb(bus, &sc->sc_hw.fs_ctl_start_pc, &sc->sc_hw.fs_ctl_start_pg,
	    sizeof(uhci_qh_t), UHCI_QH_ALIGN);

	cb(bus, &sc->sc_hw.bulk_start_pc, &sc->sc_hw.bulk_start_pg,
	    sizeof(uhci_qh_t), UHCI_QH_ALIGN);

	cb(bus, &sc->sc_hw.last_qh_pc, &sc->sc_hw.last_qh_pg,
	    sizeof(uhci_qh_t), UHCI_QH_ALIGN);

	cb(bus, &sc->sc_hw.last_td_pc, &sc->sc_hw.last_td_pg,
	    sizeof(uhci_td_t), UHCI_TD_ALIGN);

	for (i = 0; i != UHCI_VFRAMELIST_COUNT; i++) {
		cb(bus, sc->sc_hw.isoc_start_pc + i,
		    sc->sc_hw.isoc_start_pg + i,
		    sizeof(uhci_td_t), UHCI_TD_ALIGN);
	}

	for (i = 0; i != UHCI_IFRAMELIST_COUNT; i++) {
		cb(bus, sc->sc_hw.intr_start_pc + i,
		    sc->sc_hw.intr_start_pg + i,
		    sizeof(uhci_qh_t), UHCI_QH_ALIGN);
	}
}

static void
uhci_mem_layout_init(struct uhci_mem_layout *ml, struct usb_xfer *xfer)
{
	ml->buf_pc = xfer->frbuffers + 0;
	ml->fix_pc = xfer->buf_fixup;

	ml->buf_offset = 0;

	ml->max_frame_size = xfer->max_frame_size;
}

static void
uhci_mem_layout_fixup(struct uhci_mem_layout *ml, struct uhci_td *td)
{
	usbd_get_page(ml->buf_pc, ml->buf_offset, &ml->buf_res);

	if (ml->buf_res.length < td->len) {

		/* need to do a fixup */

		usbd_get_page(ml->fix_pc, 0, &ml->fix_res);

		td->td_buffer = htole32(ml->fix_res.physaddr);

		/*
	         * The UHCI driver cannot handle
	         * page crossings, so a fixup is
	         * needed:
	         *
	         *  +----+----+ - - -
	         *  | YYY|Y   |
	         *  +----+----+ - - -
	         *     \    \
	         *      \    \
	         *       +----+
	         *       |YYYY|  (fixup)
	         *       +----+
	         */

		if ((td->td_token & htole32(UHCI_TD_PID)) ==
		    htole32(UHCI_TD_PID_IN)) {
			td->fix_pc = ml->fix_pc;
			usb_pc_cpu_invalidate(ml->fix_pc);

		} else {
			td->fix_pc = NULL;

			/* copy data to fixup location */

			usbd_copy_out(ml->buf_pc, ml->buf_offset,
			    ml->fix_res.buffer, td->len);

			usb_pc_cpu_flush(ml->fix_pc);
		}

		/* prepare next fixup */

		ml->fix_pc++;

	} else {

		td->td_buffer = htole32(ml->buf_res.physaddr);
		td->fix_pc = NULL;
	}

	/* prepare next data location */

	ml->buf_offset += td->len;
}

/*
 * Return values:
 * 0: Success
 * Else: Failure
 */
static uint8_t
uhci_restart(uhci_softc_t *sc)
{
	struct usb_page_search buf_res;

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

  	if (UREAD2(sc, UHCI_CMD) & UHCI_CMD_RS) {
		DPRINTFN(2, "Already started\n");
		return (0);
	}

	DPRINTFN(2, "Restarting\n");

	usbd_get_page(&sc->sc_hw.pframes_pc, 0, &buf_res);

	/* Reload fresh base address */
	UWRITE4(sc, UHCI_FLBASEADDR, buf_res.physaddr);

	/*
	 * Assume 64 byte packets at frame end and start HC controller:
	 */
	UHCICMD(sc, (UHCI_CMD_MAXP | UHCI_CMD_RS));

	/* wait 10 milliseconds */

	usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 100);

	/* check that controller has started */

	if (UREAD2(sc, UHCI_STS) & UHCI_STS_HCH) {
		DPRINTFN(2, "Failed\n");
		return (1);
	}
	return (0);
}

void
uhci_reset(uhci_softc_t *sc)
{
	uint16_t n;

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	DPRINTF("resetting the HC\n");

	/* disable interrupts */

	UWRITE2(sc, UHCI_INTR, 0);

	/* global reset */

	UHCICMD(sc, UHCI_CMD_GRESET);

	/* wait */

	usb_pause_mtx(&sc->sc_bus.bus_mtx,
	    USB_MS_TO_TICKS(USB_BUS_RESET_DELAY));

	/* terminate all transfers */

	UHCICMD(sc, UHCI_CMD_HCRESET);

	/* the reset bit goes low when the controller is done */

	n = UHCI_RESET_TIMEOUT;
	while (n--) {
		/* wait one millisecond */

		usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 1000);

		if (!(UREAD2(sc, UHCI_CMD) & UHCI_CMD_HCRESET)) {
			goto done_1;
		}
	}

	device_printf(sc->sc_bus.bdev,
	    "controller did not reset\n");

done_1:

	n = 10;
	while (n--) {
		/* wait one millisecond */

		usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 1000);

		/* check if HC is stopped */
		if (UREAD2(sc, UHCI_STS) & UHCI_STS_HCH) {
			goto done_2;
		}
	}

	device_printf(sc->sc_bus.bdev,
	    "controller did not stop\n");

done_2:

	/* reset frame number */
	UWRITE2(sc, UHCI_FRNUM, 0);
	/* set default SOF value */
	UWRITE1(sc, UHCI_SOF, 0x40);

	USB_BUS_UNLOCK(&sc->sc_bus);

	/* stop root interrupt */
	usb_callout_drain(&sc->sc_root_intr);

	USB_BUS_LOCK(&sc->sc_bus);
}

static void
uhci_start(uhci_softc_t *sc)
{
	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	DPRINTFN(2, "enabling\n");

	/* enable interrupts */

	UWRITE2(sc, UHCI_INTR,
	    (UHCI_INTR_TOCRCIE |
	    UHCI_INTR_RIE |
	    UHCI_INTR_IOCE |
	    UHCI_INTR_SPIE));

	if (uhci_restart(sc)) {
		device_printf(sc->sc_bus.bdev,
		    "cannot start HC controller\n");
	}

	/* start root interrupt */
	uhci_root_intr(sc);
}

static struct uhci_qh *
uhci_init_qh(struct usb_page_cache *pc)
{
	struct usb_page_search buf_res;
	struct uhci_qh *qh;

	usbd_get_page(pc, 0, &buf_res);

	qh = buf_res.buffer;

	qh->qh_self =
	    htole32(buf_res.physaddr) |
	    htole32(UHCI_PTR_QH);

	qh->page_cache = pc;

	return (qh);
}

static struct uhci_td *
uhci_init_td(struct usb_page_cache *pc)
{
	struct usb_page_search buf_res;
	struct uhci_td *td;

	usbd_get_page(pc, 0, &buf_res);

	td = buf_res.buffer;

	td->td_self =
	    htole32(buf_res.physaddr) |
	    htole32(UHCI_PTR_TD);

	td->page_cache = pc;

	return (td);
}

usb_error_t
uhci_init(uhci_softc_t *sc)
{
	uint16_t bit;
	uint16_t x;
	uint16_t y;

	DPRINTF("start\n");

	usb_callout_init_mtx(&sc->sc_root_intr, &sc->sc_bus.bus_mtx, 0);

#ifdef USB_DEBUG
	if (uhcidebug > 2) {
		uhci_dumpregs(sc);
	}
#endif
	/*
	 * Setup QH's
	 */
	sc->sc_ls_ctl_p_last =
	    uhci_init_qh(&sc->sc_hw.ls_ctl_start_pc);

	sc->sc_fs_ctl_p_last =
	    uhci_init_qh(&sc->sc_hw.fs_ctl_start_pc);

	sc->sc_bulk_p_last =
	    uhci_init_qh(&sc->sc_hw.bulk_start_pc);
#if 0
	sc->sc_reclaim_qh_p =
	    sc->sc_fs_ctl_p_last;
#else
	/* setup reclaim looping point */
	sc->sc_reclaim_qh_p =
	    sc->sc_bulk_p_last;
#endif

	sc->sc_last_qh_p =
	    uhci_init_qh(&sc->sc_hw.last_qh_pc);

	sc->sc_last_td_p =
	    uhci_init_td(&sc->sc_hw.last_td_pc);

	for (x = 0; x != UHCI_VFRAMELIST_COUNT; x++) {
		sc->sc_isoc_p_last[x] =
		    uhci_init_td(sc->sc_hw.isoc_start_pc + x);
	}

	for (x = 0; x != UHCI_IFRAMELIST_COUNT; x++) {
		sc->sc_intr_p_last[x] =
		    uhci_init_qh(sc->sc_hw.intr_start_pc + x);
	}

	/*
	 * the QHs are arranged to give poll intervals that are
	 * powers of 2 times 1ms
	 */
	bit = UHCI_IFRAMELIST_COUNT / 2;
	while (bit) {
		x = bit;
		while (x & bit) {
			uhci_qh_t *qh_x;
			uhci_qh_t *qh_y;

			y = (x ^ bit) | (bit / 2);

			/*
			 * the next QH has half the poll interval
			 */
			qh_x = sc->sc_intr_p_last[x];
			qh_y = sc->sc_intr_p_last[y];

			qh_x->h_next = NULL;
			qh_x->qh_h_next = qh_y->qh_self;
			qh_x->e_next = NULL;
			qh_x->qh_e_next = htole32(UHCI_PTR_T);
			x++;
		}
		bit >>= 1;
	}

	if (1) {
		uhci_qh_t *qh_ls;
		uhci_qh_t *qh_intr;

		qh_ls = sc->sc_ls_ctl_p_last;
		qh_intr = sc->sc_intr_p_last[0];

		/* start QH for interrupt traffic */
		qh_intr->h_next = qh_ls;
		qh_intr->qh_h_next = qh_ls->qh_self;
		qh_intr->e_next = 0;
		qh_intr->qh_e_next = htole32(UHCI_PTR_T);
	}
	for (x = 0; x != UHCI_VFRAMELIST_COUNT; x++) {

		uhci_td_t *td_x;
		uhci_qh_t *qh_intr;

		td_x = sc->sc_isoc_p_last[x];
		qh_intr = sc->sc_intr_p_last[x | (UHCI_IFRAMELIST_COUNT / 2)];

		/* start TD for isochronous traffic */
		td_x->next = NULL;
		td_x->td_next = qh_intr->qh_self;
		td_x->td_status = htole32(UHCI_TD_IOS);
		td_x->td_token = htole32(0);
		td_x->td_buffer = htole32(0);
	}

	if (1) {
		uhci_qh_t *qh_ls;
		uhci_qh_t *qh_fs;

		qh_ls = sc->sc_ls_ctl_p_last;
		qh_fs = sc->sc_fs_ctl_p_last;

		/* start QH where low speed control traffic will be queued */
		qh_ls->h_next = qh_fs;
		qh_ls->qh_h_next = qh_fs->qh_self;
		qh_ls->e_next = 0;
		qh_ls->qh_e_next = htole32(UHCI_PTR_T);
	}
	if (1) {
		uhci_qh_t *qh_ctl;
		uhci_qh_t *qh_blk;
		uhci_qh_t *qh_lst;
		uhci_td_t *td_lst;

		qh_ctl = sc->sc_fs_ctl_p_last;
		qh_blk = sc->sc_bulk_p_last;

		/* start QH where full speed control traffic will be queued */
		qh_ctl->h_next = qh_blk;
		qh_ctl->qh_h_next = qh_blk->qh_self;
		qh_ctl->e_next = 0;
		qh_ctl->qh_e_next = htole32(UHCI_PTR_T);

		qh_lst = sc->sc_last_qh_p;

		/* start QH where bulk traffic will be queued */
		qh_blk->h_next = qh_lst;
		qh_blk->qh_h_next = qh_lst->qh_self;
		qh_blk->e_next = 0;
		qh_blk->qh_e_next = htole32(UHCI_PTR_T);

		td_lst = sc->sc_last_td_p;

		/* end QH which is used for looping the QHs */
		qh_lst->h_next = 0;
		qh_lst->qh_h_next = htole32(UHCI_PTR_T);	/* end of QH chain */
		qh_lst->e_next = td_lst;
		qh_lst->qh_e_next = td_lst->td_self;

		/*
		 * end TD which hangs from the last QH, to avoid a bug in the PIIX
		 * that makes it run berserk otherwise
		 */
		td_lst->next = 0;
		td_lst->td_next = htole32(UHCI_PTR_T);
		td_lst->td_status = htole32(0);	/* inactive */
		td_lst->td_token = htole32(0);
		td_lst->td_buffer = htole32(0);
	}
	if (1) {
		struct usb_page_search buf_res;
		uint32_t *pframes;

		usbd_get_page(&sc->sc_hw.pframes_pc, 0, &buf_res);

		pframes = buf_res.buffer;


		/*
		 * Setup UHCI framelist
		 *
		 * Execution order:
		 *
		 * pframes -> full speed isochronous -> interrupt QH's -> low
		 * speed control -> full speed control -> bulk transfers
		 *
		 */

		for (x = 0; x != UHCI_FRAMELIST_COUNT; x++) {
			pframes[x] =
			    sc->sc_isoc_p_last[x % UHCI_VFRAMELIST_COUNT]->td_self;
		}
	}
	/* flush all cache into memory */

	usb_bus_mem_flush_all(&sc->sc_bus, &uhci_iterate_hw_softc);

	/* set up the bus struct */
	sc->sc_bus.methods = &uhci_bus_methods;

	USB_BUS_LOCK(&sc->sc_bus);
	/* reset the controller */
	uhci_reset(sc);

	/* start the controller */
	uhci_start(sc);
	USB_BUS_UNLOCK(&sc->sc_bus);

	/* catch lost interrupts */
	uhci_do_poll(&sc->sc_bus);

	return (0);
}

static void
uhci_suspend(uhci_softc_t *sc)
{
#ifdef USB_DEBUG
	if (uhcidebug > 2) {
		uhci_dumpregs(sc);
	}
#endif

	USB_BUS_LOCK(&sc->sc_bus);

	/* stop the controller */

	uhci_reset(sc);

	/* enter global suspend */

	UHCICMD(sc, UHCI_CMD_EGSM);

	USB_BUS_UNLOCK(&sc->sc_bus);
}

static void
uhci_resume(uhci_softc_t *sc)
{
	USB_BUS_LOCK(&sc->sc_bus);

	/* reset the controller */

	uhci_reset(sc);

	/* force global resume */

	UHCICMD(sc, UHCI_CMD_FGR);

	/* and start traffic again */

	uhci_start(sc);

	USB_BUS_UNLOCK(&sc->sc_bus);

#ifdef USB_DEBUG
	if (uhcidebug > 2)
		uhci_dumpregs(sc);
#endif

	/* catch lost interrupts */
	uhci_do_poll(&sc->sc_bus);
}

#ifdef USB_DEBUG
static void
uhci_dumpregs(uhci_softc_t *sc)
{
	DPRINTFN(0, "%s regs: cmd=%04x, sts=%04x, intr=%04x, frnum=%04x, "
	    "flbase=%08x, sof=%04x, portsc1=%04x, portsc2=%04x\n",
	    device_get_nameunit(sc->sc_bus.bdev),
	    UREAD2(sc, UHCI_CMD),
	    UREAD2(sc, UHCI_STS),
	    UREAD2(sc, UHCI_INTR),
	    UREAD2(sc, UHCI_FRNUM),
	    UREAD4(sc, UHCI_FLBASEADDR),
	    UREAD1(sc, UHCI_SOF),
	    UREAD2(sc, UHCI_PORTSC1),
	    UREAD2(sc, UHCI_PORTSC2));
}

static uint8_t
uhci_dump_td(uhci_td_t *p)
{
	uint32_t td_next;
	uint32_t td_status;
	uint32_t td_token;
	uint8_t temp;

	usb_pc_cpu_invalidate(p->page_cache);

	td_next = le32toh(p->td_next);
	td_status = le32toh(p->td_status);
	td_token = le32toh(p->td_token);

	/*
	 * Check whether the link pointer in this TD marks the link pointer
	 * as end of queue:
	 */
	temp = ((td_next & UHCI_PTR_T) || (td_next == 0));

	printf("TD(%p) at 0x%08x = link=0x%08x status=0x%08x "
	    "token=0x%08x buffer=0x%08x\n",
	    p,
	    le32toh(p->td_self),
	    td_next,
	    td_status,
	    td_token,
	    le32toh(p->td_buffer));

	printf("TD(%p) td_next=%s%s%s td_status=%s%s%s%s%s%s%s%s%s%s%s, errcnt=%d, actlen=%d pid=%02x,"
	    "addr=%d,endpt=%d,D=%d,maxlen=%d\n",
	    p,
	    (td_next & 1) ? "-T" : "",
	    (td_next & 2) ? "-Q" : "",
	    (td_next & 4) ? "-VF" : "",
	    (td_status & UHCI_TD_BITSTUFF) ? "-BITSTUFF" : "",
	    (td_status & UHCI_TD_CRCTO) ? "-CRCTO" : "",
	    (td_status & UHCI_TD_NAK) ? "-NAK" : "",
	    (td_status & UHCI_TD_BABBLE) ? "-BABBLE" : "",
	    (td_status & UHCI_TD_DBUFFER) ? "-DBUFFER" : "",
	    (td_status & UHCI_TD_STALLED) ? "-STALLED" : "",
	    (td_status & UHCI_TD_ACTIVE) ? "-ACTIVE" : "",
	    (td_status & UHCI_TD_IOC) ? "-IOC" : "",
	    (td_status & UHCI_TD_IOS) ? "-IOS" : "",
	    (td_status & UHCI_TD_LS) ? "-LS" : "",
	    (td_status & UHCI_TD_SPD) ? "-SPD" : "",
	    UHCI_TD_GET_ERRCNT(td_status),
	    UHCI_TD_GET_ACTLEN(td_status),
	    UHCI_TD_GET_PID(td_token),
	    UHCI_TD_GET_DEVADDR(td_token),
	    UHCI_TD_GET_ENDPT(td_token),
	    UHCI_TD_GET_DT(td_token),
	    UHCI_TD_GET_MAXLEN(td_token));

	return (temp);
}

static uint8_t
uhci_dump_qh(uhci_qh_t *sqh)
{
	uint8_t temp;
	uint32_t qh_h_next;
	uint32_t qh_e_next;

	usb_pc_cpu_invalidate(sqh->page_cache);

	qh_h_next = le32toh(sqh->qh_h_next);
	qh_e_next = le32toh(sqh->qh_e_next);

	DPRINTFN(0, "QH(%p) at 0x%08x: h_next=0x%08x e_next=0x%08x\n", sqh,
	    le32toh(sqh->qh_self), qh_h_next, qh_e_next);

	temp = ((((sqh->h_next != NULL) && !(qh_h_next & UHCI_PTR_T)) ? 1 : 0) |
	    (((sqh->e_next != NULL) && !(qh_e_next & UHCI_PTR_T)) ? 2 : 0));

	return (temp);
}

static void
uhci_dump_all(uhci_softc_t *sc)
{
	uhci_dumpregs(sc);
	uhci_dump_qh(sc->sc_ls_ctl_p_last);
	uhci_dump_qh(sc->sc_fs_ctl_p_last);
	uhci_dump_qh(sc->sc_bulk_p_last);
	uhci_dump_qh(sc->sc_last_qh_p);
}

static void
uhci_dump_tds(uhci_td_t *td)
{
	for (;
	    td != NULL;
	    td = td->obj_next) {
		if (uhci_dump_td(td)) {
			break;
		}
	}
}

#endif

/*
 * Let the last QH loop back to the full speed control transfer QH.
 * This is what intel calls "bandwidth reclamation" and improves
 * USB performance a lot for some devices.
 * If we are already looping, just count it.
 */
static void
uhci_add_loop(uhci_softc_t *sc)
{
	struct uhci_qh *qh_lst;
	struct uhci_qh *qh_rec;

#ifdef USB_DEBUG
	if (uhcinoloop) {
		return;
	}
#endif
	if (++(sc->sc_loops) == 1) {
		DPRINTFN(6, "add\n");

		qh_lst = sc->sc_last_qh_p;
		qh_rec = sc->sc_reclaim_qh_p;

		/* NOTE: we don't loop back the soft pointer */

		qh_lst->qh_h_next = qh_rec->qh_self;
		usb_pc_cpu_flush(qh_lst->page_cache);
	}
}

static void
uhci_rem_loop(uhci_softc_t *sc)
{
	struct uhci_qh *qh_lst;

#ifdef USB_DEBUG
	if (uhcinoloop) {
		return;
	}
#endif
	if (--(sc->sc_loops) == 0) {
		DPRINTFN(6, "remove\n");

		qh_lst = sc->sc_last_qh_p;
		qh_lst->qh_h_next = htole32(UHCI_PTR_T);
		usb_pc_cpu_flush(qh_lst->page_cache);
	}
}

static void
uhci_transfer_intr_enqueue(struct usb_xfer *xfer)
{
	/* check for early completion */
	if (uhci_check_transfer(xfer)) {
		return;
	}
	/* put transfer on interrupt queue */
	usbd_transfer_enqueue(&xfer->xroot->bus->intr_q, xfer);

	/* start timeout, if any */
	if (xfer->timeout != 0) {
		usbd_transfer_timeout_ms(xfer, &uhci_timeout, xfer->timeout);
	}
}

#define	UHCI_APPEND_TD(std,last) (last) = _uhci_append_td(std,last)
static uhci_td_t *
_uhci_append_td(uhci_td_t *std, uhci_td_t *last)
{
	DPRINTFN(11, "%p to %p\n", std, last);

	/* (sc->sc_bus.mtx) must be locked */

	std->next = last->next;
	std->td_next = last->td_next;

	std->prev = last;

	usb_pc_cpu_flush(std->page_cache);

	/*
	 * the last->next->prev is never followed: std->next->prev = std;
	 */
	last->next = std;
	last->td_next = std->td_self;

	usb_pc_cpu_flush(last->page_cache);

	return (std);
}

#define	UHCI_APPEND_QH(sqh,last) (last) = _uhci_append_qh(sqh,last)
static uhci_qh_t *
_uhci_append_qh(uhci_qh_t *sqh, uhci_qh_t *last)
{
	DPRINTFN(11, "%p to %p\n", sqh, last);

	if (sqh->h_prev != NULL) {
		/* should not happen */
		DPRINTFN(0, "QH already linked!\n");
		return (last);
	}
	/* (sc->sc_bus.mtx) must be locked */

	sqh->h_next = last->h_next;
	sqh->qh_h_next = last->qh_h_next;

	sqh->h_prev = last;

	usb_pc_cpu_flush(sqh->page_cache);

	/*
	 * The "last->h_next->h_prev" is never followed:
	 *
	 * "sqh->h_next->h_prev" = sqh;
	 */

	last->h_next = sqh;
	last->qh_h_next = sqh->qh_self;

	usb_pc_cpu_flush(last->page_cache);

	return (sqh);
}

/**/

#define	UHCI_REMOVE_TD(std,last) (last) = _uhci_remove_td(std,last)
static uhci_td_t *
_uhci_remove_td(uhci_td_t *std, uhci_td_t *last)
{
	DPRINTFN(11, "%p from %p\n", std, last);

	/* (sc->sc_bus.mtx) must be locked */

	std->prev->next = std->next;
	std->prev->td_next = std->td_next;

	usb_pc_cpu_flush(std->prev->page_cache);

	if (std->next) {
		std->next->prev = std->prev;
		usb_pc_cpu_flush(std->next->page_cache);
	}
	return ((last == std) ? std->prev : last);
}

#define	UHCI_REMOVE_QH(sqh,last) (last) = _uhci_remove_qh(sqh,last)
static uhci_qh_t *
_uhci_remove_qh(uhci_qh_t *sqh, uhci_qh_t *last)
{
	DPRINTFN(11, "%p from %p\n", sqh, last);

	/* (sc->sc_bus.mtx) must be locked */

	/* only remove if not removed from a queue */
	if (sqh->h_prev) {

		sqh->h_prev->h_next = sqh->h_next;
		sqh->h_prev->qh_h_next = sqh->qh_h_next;

		usb_pc_cpu_flush(sqh->h_prev->page_cache);

		if (sqh->h_next) {
			sqh->h_next->h_prev = sqh->h_prev;
			usb_pc_cpu_flush(sqh->h_next->page_cache);
		}
		last = ((last == sqh) ? sqh->h_prev : last);

		sqh->h_prev = 0;

		usb_pc_cpu_flush(sqh->page_cache);
	}
	return (last);
}

static void
uhci_isoc_done(uhci_softc_t *sc, struct usb_xfer *xfer)
{
	struct usb_page_search res;
	uint32_t nframes = xfer->nframes;
	uint32_t status;
	uint32_t offset = 0;
	uint32_t *plen = xfer->frlengths;
	uint16_t len = 0;
	uhci_td_t *td = xfer->td_transfer_first;
	uhci_td_t **pp_last = &sc->sc_isoc_p_last[xfer->qh_pos];

	DPRINTFN(13, "xfer=%p endpoint=%p transfer done\n",
	    xfer, xfer->endpoint);

	/* sync any DMA memory before doing fixups */

	usb_bdma_post_sync(xfer);

	while (nframes--) {
		if (td == NULL) {
			panic("%s:%d: out of TD's\n",
			    __FUNCTION__, __LINE__);
		}
		if (pp_last >= &sc->sc_isoc_p_last[UHCI_VFRAMELIST_COUNT]) {
			pp_last = &sc->sc_isoc_p_last[0];
		}
#ifdef USB_DEBUG
		if (uhcidebug > 5) {
			DPRINTF("isoc TD\n");
			uhci_dump_td(td);
		}
#endif
		usb_pc_cpu_invalidate(td->page_cache);
		status = le32toh(td->td_status);

		len = UHCI_TD_GET_ACTLEN(status);

		if (len > *plen) {
			len = *plen;
		}
		if (td->fix_pc) {

			usbd_get_page(td->fix_pc, 0, &res);

			/* copy data from fixup location to real location */

			usb_pc_cpu_invalidate(td->fix_pc);

			usbd_copy_in(xfer->frbuffers, offset,
			    res.buffer, len);
		}
		offset += *plen;

		*plen = len;

		/* remove TD from schedule */
		UHCI_REMOVE_TD(td, *pp_last);

		pp_last++;
		plen++;
		td = td->obj_next;
	}

	xfer->aframes = xfer->nframes;
}

static usb_error_t
uhci_non_isoc_done_sub(struct usb_xfer *xfer)
{
	struct usb_page_search res;
	uhci_td_t *td;
	uhci_td_t *td_alt_next;
	uint32_t status;
	uint32_t token;
	uint16_t len;

	td = xfer->td_transfer_cache;
	td_alt_next = td->alt_next;

	if (xfer->aframes != xfer->nframes) {
		usbd_xfer_set_frame_len(xfer, xfer->aframes, 0);
	}
	while (1) {

		usb_pc_cpu_invalidate(td->page_cache);
		status = le32toh(td->td_status);
		token = le32toh(td->td_token);

		/*
	         * Verify the status and add
	         * up the actual length:
	         */

		len = UHCI_TD_GET_ACTLEN(status);
		if (len > td->len) {
			/* should not happen */
			DPRINTF("Invalid status length, "
			    "0x%04x/0x%04x bytes\n", len, td->len);
			status |= UHCI_TD_STALLED;

		} else if ((xfer->aframes != xfer->nframes) && (len > 0)) {

			if (td->fix_pc) {

				usbd_get_page(td->fix_pc, 0, &res);

				/*
				 * copy data from fixup location to real
				 * location
				 */

				usb_pc_cpu_invalidate(td->fix_pc);

				usbd_copy_in(xfer->frbuffers + xfer->aframes,
				    xfer->frlengths[xfer->aframes], res.buffer, len);
			}
			/* update actual length */

			xfer->frlengths[xfer->aframes] += len;
		}
		/* Check for last transfer */
		if (((void *)td) == xfer->td_transfer_last) {
			td = NULL;
			break;
		}
		if (status & UHCI_TD_STALLED) {
			/* the transfer is finished */
			td = NULL;
			break;
		}
		/* Check for short transfer */
		if (len != td->len) {
			if (xfer->flags_int.short_frames_ok) {
				/* follow alt next */
				td = td->alt_next;
			} else {
				/* the transfer is finished */
				td = NULL;
			}
			break;
		}
		td = td->obj_next;

		if (td->alt_next != td_alt_next) {
			/* this USB frame is complete */
			break;
		}
	}

	/* update transfer cache */

	xfer->td_transfer_cache = td;

	/* update data toggle */

	xfer->endpoint->toggle_next = (token & UHCI_TD_SET_DT(1)) ? 0 : 1;

#ifdef USB_DEBUG
	if (status & UHCI_TD_ERROR) {
		DPRINTFN(11, "error, addr=%d, endpt=0x%02x, frame=0x%02x "
		    "status=%s%s%s%s%s%s%s%s%s%s%s\n",
		    xfer->address, xfer->endpointno, xfer->aframes,
		    (status & UHCI_TD_BITSTUFF) ? "[BITSTUFF]" : "",
		    (status & UHCI_TD_CRCTO) ? "[CRCTO]" : "",
		    (status & UHCI_TD_NAK) ? "[NAK]" : "",
		    (status & UHCI_TD_BABBLE) ? "[BABBLE]" : "",
		    (status & UHCI_TD_DBUFFER) ? "[DBUFFER]" : "",
		    (status & UHCI_TD_STALLED) ? "[STALLED]" : "",
		    (status & UHCI_TD_ACTIVE) ? "[ACTIVE]" : "[NOT_ACTIVE]",
		    (status & UHCI_TD_IOC) ? "[IOC]" : "",
		    (status & UHCI_TD_IOS) ? "[IOS]" : "",
		    (status & UHCI_TD_LS) ? "[LS]" : "",
		    (status & UHCI_TD_SPD) ? "[SPD]" : "");
	}
#endif
	if (status & UHCI_TD_STALLED) {
		/* try to separate I/O errors from STALL */
		if (UHCI_TD_GET_ERRCNT(status) == 0)
			return (USB_ERR_IOERROR);
		return (USB_ERR_STALLED);
	}
	return (USB_ERR_NORMAL_COMPLETION);
}

static void
uhci_non_isoc_done(struct usb_xfer *xfer)
{
	usb_error_t err = 0;

	DPRINTFN(13, "xfer=%p endpoint=%p transfer done\n",
	    xfer, xfer->endpoint);

#ifdef USB_DEBUG
	if (uhcidebug > 10) {
		uhci_dump_tds(xfer->td_transfer_first);
	}
#endif

	/* sync any DMA memory before doing fixups */

	usb_bdma_post_sync(xfer);

	/* reset scanner */

	xfer->td_transfer_cache = xfer->td_transfer_first;

	if (xfer->flags_int.control_xfr) {
		if (xfer->flags_int.control_hdr) {

			err = uhci_non_isoc_done_sub(xfer);
		}
		xfer->aframes = 1;

		if (xfer->td_transfer_cache == NULL) {
			goto done;
		}
	}
	while (xfer->aframes != xfer->nframes) {

		err = uhci_non_isoc_done_sub(xfer);
		xfer->aframes++;

		if (xfer->td_transfer_cache == NULL) {
			goto done;
		}
	}

	if (xfer->flags_int.control_xfr &&
	    !xfer->flags_int.control_act) {

		err = uhci_non_isoc_done_sub(xfer);
	}
done:
	uhci_device_done(xfer, err);
}

/*------------------------------------------------------------------------*
 *	uhci_check_transfer_sub
 *
 * The main purpose of this function is to update the data-toggle
 * in case it is wrong.
 *------------------------------------------------------------------------*/
static void
uhci_check_transfer_sub(struct usb_xfer *xfer)
{
	uhci_qh_t *qh;
	uhci_td_t *td;
	uhci_td_t *td_alt_next;

	uint32_t td_token;
	uint32_t td_self;

	td = xfer->td_transfer_cache;
	qh = xfer->qh_start[xfer->flags_int.curr_dma_set];

	td_token = td->obj_next->td_token;
	td = td->alt_next;
	xfer->td_transfer_cache = td;
	td_self = td->td_self;
	td_alt_next = td->alt_next;

	if (xfer->flags_int.control_xfr)
		goto skip;	/* don't touch the DT value! */

	if (!((td->td_token ^ td_token) & htole32(UHCI_TD_SET_DT(1))))
		goto skip;	/* data toggle has correct value */

	/*
	 * The data toggle is wrong and we need to toggle it !
	 */
	while (1) {

		td->td_token ^= htole32(UHCI_TD_SET_DT(1));
		usb_pc_cpu_flush(td->page_cache);

		if (td == xfer->td_transfer_last) {
			/* last transfer */
			break;
		}
		td = td->obj_next;

		if (td->alt_next != td_alt_next) {
			/* next frame */
			break;
		}
	}
skip:

	/* update the QH */
	qh->qh_e_next = td_self;
	usb_pc_cpu_flush(qh->page_cache);

	DPRINTFN(13, "xfer=%p following alt next\n", xfer);
}

/*------------------------------------------------------------------------*
 *	uhci_check_transfer
 *
 * Return values:
 *    0: USB transfer is not finished
 * Else: USB transfer is finished
 *------------------------------------------------------------------------*/
static uint8_t
uhci_check_transfer(struct usb_xfer *xfer)
{
	uint32_t status;
	uint32_t token;
	uhci_td_t *td;

	DPRINTFN(16, "xfer=%p checking transfer\n", xfer);

	if (xfer->endpoint->methods == &uhci_device_isoc_methods) {
		/* isochronous transfer */

		td = xfer->td_transfer_last;

		usb_pc_cpu_invalidate(td->page_cache);
		status = le32toh(td->td_status);

		/* check also if the first is complete */

		td = xfer->td_transfer_first;

		usb_pc_cpu_invalidate(td->page_cache);
		status |= le32toh(td->td_status);

		if (!(status & UHCI_TD_ACTIVE)) {
			uhci_device_done(xfer, USB_ERR_NORMAL_COMPLETION);
			goto transferred;
		}
	} else {
		/* non-isochronous transfer */

		/*
		 * check whether there is an error somewhere
		 * in the middle, or whether there was a short
		 * packet (SPD and not ACTIVE)
		 */
		td = xfer->td_transfer_cache;

		while (1) {
			usb_pc_cpu_invalidate(td->page_cache);
			status = le32toh(td->td_status);
			token = le32toh(td->td_token);

			/*
			 * if there is an active TD the transfer isn't done
			 */
			if (status & UHCI_TD_ACTIVE) {
				/* update cache */
				xfer->td_transfer_cache = td;
				goto done;
			}
			/*
			 * last transfer descriptor makes the transfer done
			 */
			if (((void *)td) == xfer->td_transfer_last) {
				break;
			}
			/*
			 * any kind of error makes the transfer done
			 */
			if (status & UHCI_TD_STALLED) {
				break;
			}
			/*
			 * check if we reached the last packet
			 * or if there is a short packet:
			 */
			if ((td->td_next == htole32(UHCI_PTR_T)) ||
			    (UHCI_TD_GET_ACTLEN(status) < td->len)) {

				if (xfer->flags_int.short_frames_ok) {
					/* follow alt next */
					if (td->alt_next) {
						/* update cache */
						xfer->td_transfer_cache = td;
						uhci_check_transfer_sub(xfer);
						goto done;
					}
				}
				/* transfer is done */
				break;
			}
			td = td->obj_next;
		}
		uhci_non_isoc_done(xfer);
		goto transferred;
	}

done:
	DPRINTFN(13, "xfer=%p is still active\n", xfer);
	return (0);

transferred:
	return (1);
}

static void
uhci_interrupt_poll(uhci_softc_t *sc)
{
	struct usb_xfer *xfer;

repeat:
	TAILQ_FOREACH(xfer, &sc->sc_bus.intr_q.head, wait_entry) {
		/*
		 * check if transfer is transferred
		 */
		if (uhci_check_transfer(xfer)) {
			/* queue has been modified */
			goto repeat;
		}
	}
}

/*------------------------------------------------------------------------*
 *	uhci_interrupt - UHCI interrupt handler
 *
 * NOTE: Do not access "sc->sc_bus.bdev" inside the interrupt handler,
 * hence the interrupt handler will be setup before "sc->sc_bus.bdev"
 * is present !
 *------------------------------------------------------------------------*/
void
uhci_interrupt(uhci_softc_t *sc)
{
	uint32_t status;

	USB_BUS_LOCK(&sc->sc_bus);

	DPRINTFN(16, "real interrupt\n");

#ifdef USB_DEBUG
	if (uhcidebug > 15) {
		uhci_dumpregs(sc);
	}
#endif
	status = UREAD2(sc, UHCI_STS) & UHCI_STS_ALLINTRS;
	if (status == 0) {
		/* the interrupt was not for us */
		goto done;
	}
	if (status & (UHCI_STS_RD | UHCI_STS_HSE |
	    UHCI_STS_HCPE | UHCI_STS_HCH)) {

		if (status & UHCI_STS_RD) {
#ifdef USB_DEBUG
			printf("%s: resume detect\n",
			    __FUNCTION__);
#endif
		}
		if (status & UHCI_STS_HSE) {
			printf("%s: host system error\n",
			    __FUNCTION__);
		}
		if (status & UHCI_STS_HCPE) {
			printf("%s: host controller process error\n",
			    __FUNCTION__);
		}
		if (status & UHCI_STS_HCH) {
			/* no acknowledge needed */
			DPRINTF("%s: host controller halted\n",
			    __FUNCTION__);
#ifdef USB_DEBUG
			if (uhcidebug > 0) {
				uhci_dump_all(sc);
			}
#endif
		}
	}
	/* get acknowledge bits */
	status &= (UHCI_STS_USBINT |
	    UHCI_STS_USBEI |
	    UHCI_STS_RD |
	    UHCI_STS_HSE |
	    UHCI_STS_HCPE |
	    UHCI_STS_HCH);

	if (status == 0) {
		/* nothing to acknowledge */
		goto done;
	}
	/* acknowledge interrupts */
	UWRITE2(sc, UHCI_STS, status);

	/* poll all the USB transfers */
	uhci_interrupt_poll(sc);

done:
	USB_BUS_UNLOCK(&sc->sc_bus);
}

/*
 * called when a request does not complete
 */
static void
uhci_timeout(void *arg)
{
	struct usb_xfer *xfer = arg;

	DPRINTF("xfer=%p\n", xfer);

	USB_BUS_LOCK_ASSERT(xfer->xroot->bus, MA_OWNED);

	/* transfer is transferred */
	uhci_device_done(xfer, USB_ERR_TIMEOUT);
}

static void
uhci_do_poll(struct usb_bus *bus)
{
	struct uhci_softc *sc = UHCI_BUS2SC(bus);

	USB_BUS_LOCK(&sc->sc_bus);
	uhci_interrupt_poll(sc);
	USB_BUS_UNLOCK(&sc->sc_bus);
}

static void
uhci_setup_standard_chain_sub(struct uhci_std_temp *temp)
{
	uhci_td_t *td;
	uhci_td_t *td_next;
	uhci_td_t *td_alt_next;
	uint32_t average;
	uint32_t len_old;
	uint8_t shortpkt_old;
	uint8_t precompute;

	td_alt_next = NULL;
	shortpkt_old = temp->shortpkt;
	len_old = temp->len;
	precompute = 1;

	/* software is used to detect short incoming transfers */

	if ((temp->td_token & htole32(UHCI_TD_PID)) == htole32(UHCI_TD_PID_IN)) {
		temp->td_status |= htole32(UHCI_TD_SPD);
	} else {
		temp->td_status &= ~htole32(UHCI_TD_SPD);
	}

	temp->ml.buf_offset = 0;

restart:

	temp->td_token &= ~htole32(UHCI_TD_SET_MAXLEN(0));
	temp->td_token |= htole32(UHCI_TD_SET_MAXLEN(temp->average));

	td = temp->td;
	td_next = temp->td_next;

	while (1) {

		if (temp->len == 0) {

			if (temp->shortpkt) {
				break;
			}
			/* send a Zero Length Packet, ZLP, last */

			temp->shortpkt = 1;
			temp->td_token |= htole32(UHCI_TD_SET_MAXLEN(0));
			average = 0;

		} else {

			average = temp->average;

			if (temp->len < average) {
				temp->shortpkt = 1;
				temp->td_token &= ~htole32(UHCI_TD_SET_MAXLEN(0));
				temp->td_token |= htole32(UHCI_TD_SET_MAXLEN(temp->len));
				average = temp->len;
			}
		}

		if (td_next == NULL) {
			panic("%s: out of UHCI transfer descriptors!", __FUNCTION__);
		}
		/* get next TD */

		td = td_next;
		td_next = td->obj_next;

		/* check if we are pre-computing */

		if (precompute) {

			/* update remaining length */

			temp->len -= average;

			continue;
		}
		/* fill out current TD */

		td->td_status = temp->td_status;
		td->td_token = temp->td_token;

		/* update data toggle */

		temp->td_token ^= htole32(UHCI_TD_SET_DT(1));

		if (average == 0) {

			td->len = 0;
			td->td_buffer = 0;
			td->fix_pc = NULL;

		} else {

			/* update remaining length */

			temp->len -= average;

			td->len = average;

			/* fill out buffer pointer and do fixup, if any */

			uhci_mem_layout_fixup(&temp->ml, td);
		}

		td->alt_next = td_alt_next;

		if ((td_next == td_alt_next) && temp->setup_alt_next) {
			/* we need to receive these frames one by one ! */
			td->td_status |= htole32(UHCI_TD_IOC);
			td->td_next = htole32(UHCI_PTR_T);
		} else {
			if (td_next) {
				/* link the current TD with the next one */
				td->td_next = td_next->td_self;
			}
		}

		usb_pc_cpu_flush(td->page_cache);
	}

	if (precompute) {
		precompute = 0;

		/* setup alt next pointer, if any */
		if (temp->last_frame) {
			td_alt_next = NULL;
		} else {
			/* we use this field internally */
			td_alt_next = td_next;
		}

		/* restore */
		temp->shortpkt = shortpkt_old;
		temp->len = len_old;
		goto restart;
	}
	temp->td = td;
	temp->td_next = td_next;
}

static uhci_td_t *
uhci_setup_standard_chain(struct usb_xfer *xfer)
{
	struct uhci_std_temp temp;
	uhci_td_t *td;
	uint32_t x;

	DPRINTFN(9, "addr=%d endpt=%d sumlen=%d speed=%d\n",
	    xfer->address, UE_GET_ADDR(xfer->endpointno),
	    xfer->sumlen, usbd_get_speed(xfer->xroot->udev));

	temp.average = xfer->max_frame_size;
	temp.max_frame_size = xfer->max_frame_size;

	/* toggle the DMA set we are using */
	xfer->flags_int.curr_dma_set ^= 1;

	/* get next DMA set */
	td = xfer->td_start[xfer->flags_int.curr_dma_set];
	xfer->td_transfer_first = td;
	xfer->td_transfer_cache = td;

	temp.td = NULL;
	temp.td_next = td;
	temp.last_frame = 0;
	temp.setup_alt_next = xfer->flags_int.short_frames_ok;

	uhci_mem_layout_init(&temp.ml, xfer);

	temp.td_status =
	    htole32(UHCI_TD_ZERO_ACTLEN(UHCI_TD_SET_ERRCNT(3) |
	    UHCI_TD_ACTIVE));

	if (xfer->xroot->udev->speed == USB_SPEED_LOW) {
		temp.td_status |= htole32(UHCI_TD_LS);
	}
	temp.td_token =
	    htole32(UHCI_TD_SET_ENDPT(xfer->endpointno) |
	    UHCI_TD_SET_DEVADDR(xfer->address));

	if (xfer->endpoint->toggle_next) {
		/* DATA1 is next */
		temp.td_token |= htole32(UHCI_TD_SET_DT(1));
	}
	/* check if we should prepend a setup message */

	if (xfer->flags_int.control_xfr) {

		if (xfer->flags_int.control_hdr) {

			temp.td_token &= htole32(UHCI_TD_SET_DEVADDR(0x7F) |
			    UHCI_TD_SET_ENDPT(0xF));
			temp.td_token |= htole32(UHCI_TD_PID_SETUP |
			    UHCI_TD_SET_DT(0));

			temp.len = xfer->frlengths[0];
			temp.ml.buf_pc = xfer->frbuffers + 0;
			temp.shortpkt = temp.len ? 1 : 0;
			/* check for last frame */
			if (xfer->nframes == 1) {
				/* no STATUS stage yet, SETUP is last */
				if (xfer->flags_int.control_act) {
					temp.last_frame = 1;
					temp.setup_alt_next = 0;
				}
			}
			uhci_setup_standard_chain_sub(&temp);
		}
		x = 1;
	} else {
		x = 0;
	}

	while (x != xfer->nframes) {

		/* DATA0 / DATA1 message */

		temp.len = xfer->frlengths[x];
		temp.ml.buf_pc = xfer->frbuffers + x;

		x++;

		if (x == xfer->nframes) {
			if (xfer->flags_int.control_xfr) {
				/* no STATUS stage yet, DATA is last */
				if (xfer->flags_int.control_act) {
					temp.last_frame = 1;
					temp.setup_alt_next = 0;
				}
			} else {
				temp.last_frame = 1;
				temp.setup_alt_next = 0;
			}
		}
		/*
		 * Keep previous data toggle,
		 * device address and endpoint number:
		 */

		temp.td_token &= htole32(UHCI_TD_SET_DEVADDR(0x7F) |
		    UHCI_TD_SET_ENDPT(0xF) |
		    UHCI_TD_SET_DT(1));

		if (temp.len == 0) {

			/* make sure that we send an USB packet */

			temp.shortpkt = 0;

		} else {

			/* regular data transfer */

			temp.shortpkt = (xfer->flags.force_short_xfer) ? 0 : 1;
		}

		/* set endpoint direction */

		temp.td_token |=
		    (UE_GET_DIR(xfer->endpointno) == UE_DIR_IN) ?
		    htole32(UHCI_TD_PID_IN) :
		    htole32(UHCI_TD_PID_OUT);

		uhci_setup_standard_chain_sub(&temp);
	}

	/* check if we should append a status stage */

	if (xfer->flags_int.control_xfr &&
	    !xfer->flags_int.control_act) {

		/*
		 * send a DATA1 message and reverse the current endpoint
		 * direction
		 */

		temp.td_token &= htole32(UHCI_TD_SET_DEVADDR(0x7F) |
		    UHCI_TD_SET_ENDPT(0xF) |
		    UHCI_TD_SET_DT(1));
		temp.td_token |=
		    (UE_GET_DIR(xfer->endpointno) == UE_DIR_OUT) ?
		    htole32(UHCI_TD_PID_IN | UHCI_TD_SET_DT(1)) :
		    htole32(UHCI_TD_PID_OUT | UHCI_TD_SET_DT(1));

		temp.len = 0;
		temp.ml.buf_pc = NULL;
		temp.shortpkt = 0;
		temp.last_frame = 1;
		temp.setup_alt_next = 0;

		uhci_setup_standard_chain_sub(&temp);
	}
	td = temp.td;

	/* Ensure that last TD is terminating: */
	td->td_next = htole32(UHCI_PTR_T);

	/* set interrupt bit */

	td->td_status |= htole32(UHCI_TD_IOC);

	usb_pc_cpu_flush(td->page_cache);

	/* must have at least one frame! */

	xfer->td_transfer_last = td;

#ifdef USB_DEBUG
	if (uhcidebug > 8) {
		DPRINTF("nexttog=%d; data before transfer:\n",
		    xfer->endpoint->toggle_next);
		uhci_dump_tds(xfer->td_transfer_first);
	}
#endif
	return (xfer->td_transfer_first);
}

/* NOTE: "done" can be run two times in a row,
 * from close and from interrupt
 */

static void
uhci_device_done(struct usb_xfer *xfer, usb_error_t error)
{
	const struct usb_pipe_methods *methods = xfer->endpoint->methods;
	uhci_softc_t *sc = UHCI_BUS2SC(xfer->xroot->bus);
	uhci_qh_t *qh;

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	DPRINTFN(2, "xfer=%p, endpoint=%p, error=%d\n",
	    xfer, xfer->endpoint, error);

	qh = xfer->qh_start[xfer->flags_int.curr_dma_set];
	if (qh) {
		usb_pc_cpu_invalidate(qh->page_cache);
	}
	if (xfer->flags_int.bandwidth_reclaimed) {
		xfer->flags_int.bandwidth_reclaimed = 0;
		uhci_rem_loop(sc);
	}
	if (methods == &uhci_device_bulk_methods) {
		UHCI_REMOVE_QH(qh, sc->sc_bulk_p_last);
	}
	if (methods == &uhci_device_ctrl_methods) {
		if (xfer->xroot->udev->speed == USB_SPEED_LOW) {
			UHCI_REMOVE_QH(qh, sc->sc_ls_ctl_p_last);
		} else {
			UHCI_REMOVE_QH(qh, sc->sc_fs_ctl_p_last);
		}
	}
	if (methods == &uhci_device_intr_methods) {
		UHCI_REMOVE_QH(qh, sc->sc_intr_p_last[xfer->qh_pos]);
	}
	/*
	 * Only finish isochronous transfers once
	 * which will update "xfer->frlengths".
	 */
	if (xfer->td_transfer_first &&
	    xfer->td_transfer_last) {
		if (methods == &uhci_device_isoc_methods) {
			uhci_isoc_done(sc, xfer);
		}
		xfer->td_transfer_first = NULL;
		xfer->td_transfer_last = NULL;
	}
	/* dequeue transfer and start next transfer */
	usbd_transfer_done(xfer, error);
}

/*------------------------------------------------------------------------*
 * uhci bulk support
 *------------------------------------------------------------------------*/
static void
uhci_device_bulk_open(struct usb_xfer *xfer)
{
	return;
}

static void
uhci_device_bulk_close(struct usb_xfer *xfer)
{
	uhci_device_done(xfer, USB_ERR_CANCELLED);
}

static void
uhci_device_bulk_enter(struct usb_xfer *xfer)
{
	return;
}

static void
uhci_device_bulk_start(struct usb_xfer *xfer)
{
	uhci_softc_t *sc = UHCI_BUS2SC(xfer->xroot->bus);
	uhci_td_t *td;
	uhci_qh_t *qh;

	/* setup TD's */
	td = uhci_setup_standard_chain(xfer);

	/* setup QH */
	qh = xfer->qh_start[xfer->flags_int.curr_dma_set];

	qh->e_next = td;
	qh->qh_e_next = td->td_self;

	if (xfer->xroot->udev->flags.self_suspended == 0) {
		UHCI_APPEND_QH(qh, sc->sc_bulk_p_last);
		uhci_add_loop(sc);
		xfer->flags_int.bandwidth_reclaimed = 1;
	} else {
		usb_pc_cpu_flush(qh->page_cache);
	}

	/* put transfer on interrupt queue */
	uhci_transfer_intr_enqueue(xfer);
}

static const struct usb_pipe_methods uhci_device_bulk_methods =
{
	.open = uhci_device_bulk_open,
	.close = uhci_device_bulk_close,
	.enter = uhci_device_bulk_enter,
	.start = uhci_device_bulk_start,
};

/*------------------------------------------------------------------------*
 * uhci control support
 *------------------------------------------------------------------------*/
static void
uhci_device_ctrl_open(struct usb_xfer *xfer)
{
	return;
}

static void
uhci_device_ctrl_close(struct usb_xfer *xfer)
{
	uhci_device_done(xfer, USB_ERR_CANCELLED);
}

static void
uhci_device_ctrl_enter(struct usb_xfer *xfer)
{
	return;
}

static void
uhci_device_ctrl_start(struct usb_xfer *xfer)
{
	uhci_softc_t *sc = UHCI_BUS2SC(xfer->xroot->bus);
	uhci_qh_t *qh;
	uhci_td_t *td;

	/* setup TD's */
	td = uhci_setup_standard_chain(xfer);

	/* setup QH */
	qh = xfer->qh_start[xfer->flags_int.curr_dma_set];

	qh->e_next = td;
	qh->qh_e_next = td->td_self;

	/*
	 * NOTE: some devices choke on bandwidth- reclamation for control
	 * transfers
	 */
	if (xfer->xroot->udev->flags.self_suspended == 0) {
		if (xfer->xroot->udev->speed == USB_SPEED_LOW) {
			UHCI_APPEND_QH(qh, sc->sc_ls_ctl_p_last);
		} else {
			UHCI_APPEND_QH(qh, sc->sc_fs_ctl_p_last);
		}
	} else {
		usb_pc_cpu_flush(qh->page_cache);
	}
	/* put transfer on interrupt queue */
	uhci_transfer_intr_enqueue(xfer);
}

static const struct usb_pipe_methods uhci_device_ctrl_methods =
{
	.open = uhci_device_ctrl_open,
	.close = uhci_device_ctrl_close,
	.enter = uhci_device_ctrl_enter,
	.start = uhci_device_ctrl_start,
};

/*------------------------------------------------------------------------*
 * uhci interrupt support
 *------------------------------------------------------------------------*/
static void
uhci_device_intr_open(struct usb_xfer *xfer)
{
	uhci_softc_t *sc = UHCI_BUS2SC(xfer->xroot->bus);
	uint16_t best;
	uint16_t bit;
	uint16_t x;

	best = 0;
	bit = UHCI_IFRAMELIST_COUNT / 2;
	while (bit) {
		if (xfer->interval >= bit) {
			x = bit;
			best = bit;
			while (x & bit) {
				if (sc->sc_intr_stat[x] <
				    sc->sc_intr_stat[best]) {
					best = x;
				}
				x++;
			}
			break;
		}
		bit >>= 1;
	}

	sc->sc_intr_stat[best]++;
	xfer->qh_pos = best;

	DPRINTFN(3, "best=%d interval=%d\n",
	    best, xfer->interval);
}

static void
uhci_device_intr_close(struct usb_xfer *xfer)
{
	uhci_softc_t *sc = UHCI_BUS2SC(xfer->xroot->bus);

	sc->sc_intr_stat[xfer->qh_pos]--;

	uhci_device_done(xfer, USB_ERR_CANCELLED);
}

static void
uhci_device_intr_enter(struct usb_xfer *xfer)
{
	return;
}

static void
uhci_device_intr_start(struct usb_xfer *xfer)
{
	uhci_softc_t *sc = UHCI_BUS2SC(xfer->xroot->bus);
	uhci_qh_t *qh;
	uhci_td_t *td;

	/* setup TD's */
	td = uhci_setup_standard_chain(xfer);

	/* setup QH */
	qh = xfer->qh_start[xfer->flags_int.curr_dma_set];

	qh->e_next = td;
	qh->qh_e_next = td->td_self;

	if (xfer->xroot->udev->flags.self_suspended == 0) {
		/* enter QHs into the controller data structures */
		UHCI_APPEND_QH(qh, sc->sc_intr_p_last[xfer->qh_pos]);
	} else {
		usb_pc_cpu_flush(qh->page_cache);
	}

	/* put transfer on interrupt queue */
	uhci_transfer_intr_enqueue(xfer);
}

static const struct usb_pipe_methods uhci_device_intr_methods =
{
	.open = uhci_device_intr_open,
	.close = uhci_device_intr_close,
	.enter = uhci_device_intr_enter,
	.start = uhci_device_intr_start,
};

/*------------------------------------------------------------------------*
 * uhci isochronous support
 *------------------------------------------------------------------------*/
static void
uhci_device_isoc_open(struct usb_xfer *xfer)
{
	uhci_td_t *td;
	uint32_t td_token;
	uint8_t ds;

	td_token =
	    (UE_GET_DIR(xfer->endpointno) == UE_DIR_IN) ?
	    UHCI_TD_IN(0, xfer->endpointno, xfer->address, 0) :
	    UHCI_TD_OUT(0, xfer->endpointno, xfer->address, 0);

	td_token = htole32(td_token);

	/* initialize all TD's */

	for (ds = 0; ds != 2; ds++) {

		for (td = xfer->td_start[ds]; td; td = td->obj_next) {

			/* mark TD as inactive */
			td->td_status = htole32(UHCI_TD_IOS);
			td->td_token = td_token;

			usb_pc_cpu_flush(td->page_cache);
		}
	}
}

static void
uhci_device_isoc_close(struct usb_xfer *xfer)
{
	uhci_device_done(xfer, USB_ERR_CANCELLED);
}

static void
uhci_device_isoc_enter(struct usb_xfer *xfer)
{
	struct uhci_mem_layout ml;
	uhci_softc_t *sc = UHCI_BUS2SC(xfer->xroot->bus);
	uint32_t nframes;
	uint32_t temp;
	uint32_t *plen;

#ifdef USB_DEBUG
	uint8_t once = 1;

#endif
	uhci_td_t *td;
	uhci_td_t *td_last = NULL;
	uhci_td_t **pp_last;

	DPRINTFN(6, "xfer=%p next=%d nframes=%d\n",
	    xfer, xfer->endpoint->isoc_next, xfer->nframes);

	nframes = UREAD2(sc, UHCI_FRNUM);

	temp = (nframes - xfer->endpoint->isoc_next) &
	    (UHCI_VFRAMELIST_COUNT - 1);

	if ((xfer->endpoint->is_synced == 0) ||
	    (temp < xfer->nframes)) {
		/*
		 * If there is data underflow or the pipe queue is empty we
		 * schedule the transfer a few frames ahead of the current
		 * frame position. Else two isochronous transfers might
		 * overlap.
		 */
		xfer->endpoint->isoc_next = (nframes + 3) & (UHCI_VFRAMELIST_COUNT - 1);
		xfer->endpoint->is_synced = 1;
		DPRINTFN(3, "start next=%d\n", xfer->endpoint->isoc_next);
	}
	/*
	 * compute how many milliseconds the insertion is ahead of the
	 * current frame position:
	 */
	temp = (xfer->endpoint->isoc_next - nframes) &
	    (UHCI_VFRAMELIST_COUNT - 1);

	/*
	 * pre-compute when the isochronous transfer will be finished:
	 */
	xfer->isoc_time_complete =
	    usb_isoc_time_expand(&sc->sc_bus, nframes) + temp +
	    xfer->nframes;

	/* get the real number of frames */

	nframes = xfer->nframes;

	uhci_mem_layout_init(&ml, xfer);

	plen = xfer->frlengths;

	/* toggle the DMA set we are using */
	xfer->flags_int.curr_dma_set ^= 1;

	/* get next DMA set */
	td = xfer->td_start[xfer->flags_int.curr_dma_set];
	xfer->td_transfer_first = td;

	pp_last = &sc->sc_isoc_p_last[xfer->endpoint->isoc_next];

	/* store starting position */

	xfer->qh_pos = xfer->endpoint->isoc_next;

	while (nframes--) {
		if (td == NULL) {
			panic("%s:%d: out of TD's\n",
			    __FUNCTION__, __LINE__);
		}
		if (pp_last >= &sc->sc_isoc_p_last[UHCI_VFRAMELIST_COUNT]) {
			pp_last = &sc->sc_isoc_p_last[0];
		}
		if (*plen > xfer->max_frame_size) {
#ifdef USB_DEBUG
			if (once) {
				once = 0;
				printf("%s: frame length(%d) exceeds %d "
				    "bytes (frame truncated)\n",
				    __FUNCTION__, *plen,
				    xfer->max_frame_size);
			}
#endif
			*plen = xfer->max_frame_size;
		}
		/* reuse td_token from last transfer */

		td->td_token &= htole32(~UHCI_TD_MAXLEN_MASK);
		td->td_token |= htole32(UHCI_TD_SET_MAXLEN(*plen));

		td->len = *plen;

		if (td->len == 0) {
			/*
			 * Do not call "uhci_mem_layout_fixup()" when the
			 * length is zero!
			 */
			td->td_buffer = 0;
			td->fix_pc = NULL;

		} else {

			/* fill out buffer pointer and do fixup, if any */

			uhci_mem_layout_fixup(&ml, td);

		}

		/* update status */
		if (nframes == 0) {
			td->td_status = htole32
			    (UHCI_TD_ZERO_ACTLEN
			    (UHCI_TD_SET_ERRCNT(0) |
			    UHCI_TD_ACTIVE |
			    UHCI_TD_IOS |
			    UHCI_TD_IOC));
		} else {
			td->td_status = htole32
			    (UHCI_TD_ZERO_ACTLEN
			    (UHCI_TD_SET_ERRCNT(0) |
			    UHCI_TD_ACTIVE |
			    UHCI_TD_IOS));
		}

		usb_pc_cpu_flush(td->page_cache);

#ifdef USB_DEBUG
		if (uhcidebug > 5) {
			DPRINTF("TD %d\n", nframes);
			uhci_dump_td(td);
		}
#endif
		/* insert TD into schedule */
		UHCI_APPEND_TD(td, *pp_last);
		pp_last++;

		plen++;
		td_last = td;
		td = td->obj_next;
	}

	xfer->td_transfer_last = td_last;

	/* update isoc_next */
	xfer->endpoint->isoc_next = (pp_last - &sc->sc_isoc_p_last[0]) &
	    (UHCI_VFRAMELIST_COUNT - 1);
}

static void
uhci_device_isoc_start(struct usb_xfer *xfer)
{
	/* put transfer on interrupt queue */
	uhci_transfer_intr_enqueue(xfer);
}

static const struct usb_pipe_methods uhci_device_isoc_methods =
{
	.open = uhci_device_isoc_open,
	.close = uhci_device_isoc_close,
	.enter = uhci_device_isoc_enter,
	.start = uhci_device_isoc_start,
};

/*------------------------------------------------------------------------*
 * uhci root control support
 *------------------------------------------------------------------------*
 * Simulate a hardware hub by handling all the necessary requests.
 *------------------------------------------------------------------------*/

static const
struct usb_device_descriptor uhci_devd =
{
	sizeof(struct usb_device_descriptor),
	UDESC_DEVICE,			/* type */
	{0x00, 0x01},			/* USB version */
	UDCLASS_HUB,			/* class */
	UDSUBCLASS_HUB,			/* subclass */
	UDPROTO_FSHUB,			/* protocol */
	64,				/* max packet */
	{0}, {0}, {0x00, 0x01},		/* device id */
	1, 2, 0,			/* string indexes */
	1				/* # of configurations */
};

static const struct uhci_config_desc uhci_confd = {
	.confd = {
		.bLength = sizeof(struct usb_config_descriptor),
		.bDescriptorType = UDESC_CONFIG,
		.wTotalLength[0] = sizeof(uhci_confd),
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
		.bEndpointAddress = UE_DIR_IN | UHCI_INTR_ENDPT,
		.bmAttributes = UE_INTERRUPT,
		.wMaxPacketSize[0] = 8,	/* max packet (63 ports) */
		.bInterval = 255,
	},
};

static const
struct usb_hub_descriptor_min uhci_hubd_piix =
{
	.bDescLength = sizeof(uhci_hubd_piix),
	.bDescriptorType = UDESC_HUB,
	.bNbrPorts = 2,
	.wHubCharacteristics = {UHD_PWR_NO_SWITCH | UHD_OC_INDIVIDUAL, 0},
	.bPwrOn2PwrGood = 50,
};

/*
 * The USB hub protocol requires that SET_FEATURE(PORT_RESET) also
 * enables the port, and also states that SET_FEATURE(PORT_ENABLE)
 * should not be used by the USB subsystem.  As we cannot issue a
 * SET_FEATURE(PORT_ENABLE) externally, we must ensure that the port
 * will be enabled as part of the reset.
 *
 * On the VT83C572, the port cannot be successfully enabled until the
 * outstanding "port enable change" and "connection status change"
 * events have been reset.
 */
static usb_error_t
uhci_portreset(uhci_softc_t *sc, uint16_t index)
{
	uint16_t port;
	uint16_t x;
	uint8_t lim;

	if (index == 1)
		port = UHCI_PORTSC1;
	else if (index == 2)
		port = UHCI_PORTSC2;
	else
		return (USB_ERR_IOERROR);

	/*
	 * Before we do anything, turn on SOF messages on the USB
	 * BUS. Some USB devices do not cope without them!
	 */
	uhci_restart(sc);

	x = URWMASK(UREAD2(sc, port));
	UWRITE2(sc, port, x | UHCI_PORTSC_PR);

	usb_pause_mtx(&sc->sc_bus.bus_mtx,
	    USB_MS_TO_TICKS(usb_port_root_reset_delay));

	DPRINTFN(4, "uhci port %d reset, status0 = 0x%04x\n",
	    index, UREAD2(sc, port));

	x = URWMASK(UREAD2(sc, port));
	UWRITE2(sc, port, x & ~UHCI_PORTSC_PR);


	mtx_unlock(&sc->sc_bus.bus_mtx);

	/* 
	 * This delay needs to be exactly 100us, else some USB devices
	 * fail to attach!
	 */
	DELAY(100);

	mtx_lock(&sc->sc_bus.bus_mtx);

	DPRINTFN(4, "uhci port %d reset, status1 = 0x%04x\n",
	    index, UREAD2(sc, port));

	x = URWMASK(UREAD2(sc, port));
	UWRITE2(sc, port, x | UHCI_PORTSC_PE);

	for (lim = 0; lim < 12; lim++) {

		usb_pause_mtx(&sc->sc_bus.bus_mtx,
		    USB_MS_TO_TICKS(usb_port_reset_delay));

		x = UREAD2(sc, port);

		DPRINTFN(4, "uhci port %d iteration %u, status = 0x%04x\n",
		    index, lim, x);

		if (!(x & UHCI_PORTSC_CCS)) {
			/*
			 * No device is connected (or was disconnected
			 * during reset).  Consider the port reset.
			 * The delay must be long enough to ensure on
			 * the initial iteration that the device
			 * connection will have been registered.  50ms
			 * appears to be sufficient, but 20ms is not.
			 */
			DPRINTFN(4, "uhci port %d loop %u, device detached\n",
			    index, lim);
			goto done;
		}
		if (x & (UHCI_PORTSC_POEDC | UHCI_PORTSC_CSC)) {
			/*
			 * Port enabled changed and/or connection
			 * status changed were set.  Reset either or
			 * both raised flags (by writing a 1 to that
			 * bit), and wait again for state to settle.
			 */
			UWRITE2(sc, port, URWMASK(x) |
			    (x & (UHCI_PORTSC_POEDC | UHCI_PORTSC_CSC)));
			continue;
		}
		if (x & UHCI_PORTSC_PE) {
			/* port is enabled */
			goto done;
		}
		UWRITE2(sc, port, URWMASK(x) | UHCI_PORTSC_PE);
	}

	DPRINTFN(2, "uhci port %d reset timed out\n", index);
	return (USB_ERR_TIMEOUT);

done:
	DPRINTFN(4, "uhci port %d reset, status2 = 0x%04x\n",
	    index, UREAD2(sc, port));

	sc->sc_isreset = 1;
	return (USB_ERR_NORMAL_COMPLETION);
}

static usb_error_t
uhci_roothub_exec(struct usb_device *udev,
    struct usb_device_request *req, const void **pptr, uint16_t *plength)
{
	uhci_softc_t *sc = UHCI_BUS2SC(udev->bus);
	const void *ptr;
	const char *str_ptr;
	uint16_t x;
	uint16_t port;
	uint16_t value;
	uint16_t index;
	uint16_t status;
	uint16_t change;
	uint16_t len;
	usb_error_t err;

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

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
		/*
		 * DEVICE_REMOTE_WAKEUP and ENDPOINT_HALT are no-ops
		 * for the integrated root hub.
		 */
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
			len = sizeof(uhci_devd);
			ptr = (const void *)&uhci_devd;
			break;

		case UDESC_CONFIG:
			if ((value & 0xff) != 0) {
				err = USB_ERR_IOERROR;
				goto done;
			}
			len = sizeof(uhci_confd);
			ptr = (const void *)&uhci_confd;
			break;

		case UDESC_STRING:
			switch (value & 0xff) {
			case 0:	/* Language table */
				str_ptr = "\001";
				break;

			case 1:	/* Vendor */
				str_ptr = sc->sc_vendor;
				break;

			case 2:	/* Product */
				str_ptr = "UHCI root HUB";
				break;

			default:
				str_ptr = "";
				break;
			}

			len = usb_make_str_desc
			    (sc->sc_hub_desc.temp,
			    sizeof(sc->sc_hub_desc.temp),
			    str_ptr);
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
		if (value >= UHCI_MAX_DEVICES) {
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
		if (index == 1)
			port = UHCI_PORTSC1;
		else if (index == 2)
			port = UHCI_PORTSC2;
		else {
			err = USB_ERR_IOERROR;
			goto done;
		}
		switch (value) {
		case UHF_PORT_ENABLE:
			x = URWMASK(UREAD2(sc, port));
			UWRITE2(sc, port, x & ~UHCI_PORTSC_PE);
			break;
		case UHF_PORT_SUSPEND:
			x = URWMASK(UREAD2(sc, port));
			UWRITE2(sc, port, x & ~(UHCI_PORTSC_SUSP));
			break;
		case UHF_PORT_RESET:
			x = URWMASK(UREAD2(sc, port));
			UWRITE2(sc, port, x & ~UHCI_PORTSC_PR);
			break;
		case UHF_C_PORT_CONNECTION:
			x = URWMASK(UREAD2(sc, port));
			UWRITE2(sc, port, x | UHCI_PORTSC_CSC);
			break;
		case UHF_C_PORT_ENABLE:
			x = URWMASK(UREAD2(sc, port));
			UWRITE2(sc, port, x | UHCI_PORTSC_POEDC);
			break;
		case UHF_C_PORT_OVER_CURRENT:
			x = URWMASK(UREAD2(sc, port));
			UWRITE2(sc, port, x | UHCI_PORTSC_OCIC);
			break;
		case UHF_C_PORT_RESET:
			sc->sc_isreset = 0;
			err = USB_ERR_NORMAL_COMPLETION;
			goto done;
		case UHF_C_PORT_SUSPEND:
			sc->sc_isresumed &= ~(1 << index);
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
	case C(UR_GET_BUS_STATE, UT_READ_CLASS_OTHER):
		if (index == 1)
			port = UHCI_PORTSC1;
		else if (index == 2)
			port = UHCI_PORTSC2;
		else {
			err = USB_ERR_IOERROR;
			goto done;
		}
		len = 1;
		sc->sc_hub_desc.temp[0] =
		    ((UREAD2(sc, port) & UHCI_PORTSC_LS) >>
		    UHCI_PORTSC_LS_SHIFT);
		break;
	case C(UR_GET_DESCRIPTOR, UT_READ_CLASS_DEVICE):
		if ((value & 0xff) != 0) {
			err = USB_ERR_IOERROR;
			goto done;
		}
		len = sizeof(uhci_hubd_piix);
		ptr = (const void *)&uhci_hubd_piix;
		break;
	case C(UR_GET_STATUS, UT_READ_CLASS_DEVICE):
		len = 16;
		memset(sc->sc_hub_desc.temp, 0, 16);
		break;
	case C(UR_GET_STATUS, UT_READ_CLASS_OTHER):
		if (index == 1)
			port = UHCI_PORTSC1;
		else if (index == 2)
			port = UHCI_PORTSC2;
		else {
			err = USB_ERR_IOERROR;
			goto done;
		}
		x = UREAD2(sc, port);
		status = change = 0;
		if (x & UHCI_PORTSC_CCS)
			status |= UPS_CURRENT_CONNECT_STATUS;
		if (x & UHCI_PORTSC_CSC)
			change |= UPS_C_CONNECT_STATUS;
		if (x & UHCI_PORTSC_PE)
			status |= UPS_PORT_ENABLED;
		if (x & UHCI_PORTSC_POEDC)
			change |= UPS_C_PORT_ENABLED;
		if (x & UHCI_PORTSC_OCI)
			status |= UPS_OVERCURRENT_INDICATOR;
		if (x & UHCI_PORTSC_OCIC)
			change |= UPS_C_OVERCURRENT_INDICATOR;
		if (x & UHCI_PORTSC_LSDA)
			status |= UPS_LOW_SPEED;
		if ((x & UHCI_PORTSC_PE) && (x & UHCI_PORTSC_RD)) {
			/* need to do a write back */
			UWRITE2(sc, port, URWMASK(x));

			/* wait 20ms for resume sequence to complete */
			usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 50);

			/* clear suspend and resume detect */
			UWRITE2(sc, port, URWMASK(x) & ~(UHCI_PORTSC_RD |
			    UHCI_PORTSC_SUSP));

			/* wait a little bit */
			usb_pause_mtx(&sc->sc_bus.bus_mtx, hz / 500);

			sc->sc_isresumed |= (1 << index);

		} else if (x & UHCI_PORTSC_SUSP) {
			status |= UPS_SUSPEND;
		}
		status |= UPS_PORT_POWER;
		if (sc->sc_isresumed & (1 << index))
			change |= UPS_C_SUSPEND;
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
		if (index == 1)
			port = UHCI_PORTSC1;
		else if (index == 2)
			port = UHCI_PORTSC2;
		else {
			err = USB_ERR_IOERROR;
			goto done;
		}
		switch (value) {
		case UHF_PORT_ENABLE:
			x = URWMASK(UREAD2(sc, port));
			UWRITE2(sc, port, x | UHCI_PORTSC_PE);
			break;
		case UHF_PORT_SUSPEND:
			x = URWMASK(UREAD2(sc, port));
			UWRITE2(sc, port, x | UHCI_PORTSC_SUSP);
			break;
		case UHF_PORT_RESET:
			err = uhci_portreset(sc, index);
			goto done;
		case UHF_PORT_POWER:
			/* pretend we turned on power */
			err = USB_ERR_NORMAL_COMPLETION;
			goto done;
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

/*
 * This routine is executed periodically and simulates interrupts from
 * the root controller interrupt pipe for port status change:
 */
static void
uhci_root_intr(uhci_softc_t *sc)
{
	DPRINTFN(21, "\n");

	USB_BUS_LOCK_ASSERT(&sc->sc_bus, MA_OWNED);

	sc->sc_hub_idata[0] = 0;

	if (UREAD2(sc, UHCI_PORTSC1) & (UHCI_PORTSC_CSC |
	    UHCI_PORTSC_OCIC | UHCI_PORTSC_RD)) {
		sc->sc_hub_idata[0] |= 1 << 1;
	}
	if (UREAD2(sc, UHCI_PORTSC2) & (UHCI_PORTSC_CSC |
	    UHCI_PORTSC_OCIC | UHCI_PORTSC_RD)) {
		sc->sc_hub_idata[0] |= 1 << 2;
	}

	/* restart timer */
	usb_callout_reset(&sc->sc_root_intr, hz,
	    (void *)&uhci_root_intr, sc);

	if (sc->sc_hub_idata[0] != 0) {
		uhub_root_intr(&sc->sc_bus, sc->sc_hub_idata,
		    sizeof(sc->sc_hub_idata));
	}
}

static void
uhci_xfer_setup(struct usb_setup_params *parm)
{
	struct usb_page_search page_info;
	struct usb_page_cache *pc;
	uhci_softc_t *sc;
	struct usb_xfer *xfer;
	void *last_obj;
	uint32_t ntd;
	uint32_t nqh;
	uint32_t nfixup;
	uint32_t n;
	uint16_t align;

	sc = UHCI_BUS2SC(parm->udev->bus);
	xfer = parm->curr_xfer;

	parm->hc_max_packet_size = 0x500;
	parm->hc_max_packet_count = 1;
	parm->hc_max_frame_size = 0x500;

	/*
	 * compute ntd and nqh
	 */
	if (parm->methods == &uhci_device_ctrl_methods) {
		xfer->flags_int.bdma_enable = 1;
		xfer->flags_int.bdma_no_post_sync = 1;

		usbd_transfer_setup_sub(parm);

		/* see EHCI HC driver for proof of "ntd" formula */

		nqh = 1;
		ntd = ((2 * xfer->nframes) + 1	/* STATUS */
		    + (xfer->max_data_length / xfer->max_frame_size));

	} else if (parm->methods == &uhci_device_bulk_methods) {
		xfer->flags_int.bdma_enable = 1;
		xfer->flags_int.bdma_no_post_sync = 1;

		usbd_transfer_setup_sub(parm);

		nqh = 1;
		ntd = ((2 * xfer->nframes)
		    + (xfer->max_data_length / xfer->max_frame_size));

	} else if (parm->methods == &uhci_device_intr_methods) {
		xfer->flags_int.bdma_enable = 1;
		xfer->flags_int.bdma_no_post_sync = 1;

		usbd_transfer_setup_sub(parm);

		nqh = 1;
		ntd = ((2 * xfer->nframes)
		    + (xfer->max_data_length / xfer->max_frame_size));

	} else if (parm->methods == &uhci_device_isoc_methods) {
		xfer->flags_int.bdma_enable = 1;
		xfer->flags_int.bdma_no_post_sync = 1;

		usbd_transfer_setup_sub(parm);

		nqh = 0;
		ntd = xfer->nframes;

	} else {

		usbd_transfer_setup_sub(parm);

		nqh = 0;
		ntd = 0;
	}

	if (parm->err) {
		return;
	}
	/*
	 * NOTE: the UHCI controller requires that
	 * every packet must be contiguous on
	 * the same USB memory page !
	 */
	nfixup = (parm->bufsize / USB_PAGE_SIZE) + 1;

	/*
	 * Compute a suitable power of two alignment
	 * for our "max_frame_size" fixup buffer(s):
	 */
	align = xfer->max_frame_size;
	n = 0;
	while (align) {
		align >>= 1;
		n++;
	}

	/* check for power of two */
	if (!(xfer->max_frame_size &
	    (xfer->max_frame_size - 1))) {
		n--;
	}
	/*
	 * We don't allow alignments of
	 * less than 8 bytes:
	 *
	 * NOTE: Allocating using an aligment
	 * of 1 byte has special meaning!
	 */
	if (n < 3) {
		n = 3;
	}
	align = (1 << n);

	if (usbd_transfer_setup_sub_malloc(
	    parm, &pc, xfer->max_frame_size,
	    align, nfixup)) {
		parm->err = USB_ERR_NOMEM;
		return;
	}
	xfer->buf_fixup = pc;

alloc_dma_set:

	if (parm->err) {
		return;
	}
	last_obj = NULL;

	if (usbd_transfer_setup_sub_malloc(
	    parm, &pc, sizeof(uhci_td_t),
	    UHCI_TD_ALIGN, ntd)) {
		parm->err = USB_ERR_NOMEM;
		return;
	}
	if (parm->buf) {
		for (n = 0; n != ntd; n++) {
			uhci_td_t *td;

			usbd_get_page(pc + n, 0, &page_info);

			td = page_info.buffer;

			/* init TD */
			if ((parm->methods == &uhci_device_bulk_methods) ||
			    (parm->methods == &uhci_device_ctrl_methods) ||
			    (parm->methods == &uhci_device_intr_methods)) {
				/* set depth first bit */
				td->td_self = htole32(page_info.physaddr |
				    UHCI_PTR_TD | UHCI_PTR_VF);
			} else {
				td->td_self = htole32(page_info.physaddr |
				    UHCI_PTR_TD);
			}

			td->obj_next = last_obj;
			td->page_cache = pc + n;

			last_obj = td;

			usb_pc_cpu_flush(pc + n);
		}
	}
	xfer->td_start[xfer->flags_int.curr_dma_set] = last_obj;

	last_obj = NULL;

	if (usbd_transfer_setup_sub_malloc(
	    parm, &pc, sizeof(uhci_qh_t),
	    UHCI_QH_ALIGN, nqh)) {
		parm->err = USB_ERR_NOMEM;
		return;
	}
	if (parm->buf) {
		for (n = 0; n != nqh; n++) {
			uhci_qh_t *qh;

			usbd_get_page(pc + n, 0, &page_info);

			qh = page_info.buffer;

			/* init QH */
			qh->qh_self = htole32(page_info.physaddr | UHCI_PTR_QH);
			qh->obj_next = last_obj;
			qh->page_cache = pc + n;

			last_obj = qh;

			usb_pc_cpu_flush(pc + n);
		}
	}
	xfer->qh_start[xfer->flags_int.curr_dma_set] = last_obj;

	if (!xfer->flags_int.curr_dma_set) {
		xfer->flags_int.curr_dma_set = 1;
		goto alloc_dma_set;
	}
}

static void
uhci_ep_init(struct usb_device *udev, struct usb_endpoint_descriptor *edesc,
    struct usb_endpoint *ep)
{
	uhci_softc_t *sc = UHCI_BUS2SC(udev->bus);

	DPRINTFN(2, "endpoint=%p, addr=%d, endpt=%d, mode=%d (%d)\n",
	    ep, udev->address,
	    edesc->bEndpointAddress, udev->flags.usb_mode,
	    sc->sc_addr);

	if (udev->device_index != sc->sc_addr) {
		switch (edesc->bmAttributes & UE_XFERTYPE) {
		case UE_CONTROL:
			ep->methods = &uhci_device_ctrl_methods;
			break;
		case UE_INTERRUPT:
			ep->methods = &uhci_device_intr_methods;
			break;
		case UE_ISOCHRONOUS:
			if (udev->speed == USB_SPEED_FULL) {
				ep->methods = &uhci_device_isoc_methods;
			}
			break;
		case UE_BULK:
			ep->methods = &uhci_device_bulk_methods;
			break;
		default:
			/* do nothing */
			break;
		}
	}
}

static void
uhci_xfer_unsetup(struct usb_xfer *xfer)
{
	return;
}

static void
uhci_get_dma_delay(struct usb_device *udev, uint32_t *pus)
{
	/*
	 * Wait until hardware has finished any possible use of the
	 * transfer descriptor(s) and QH
	 */
	*pus = (1125);			/* microseconds */
}

static void
uhci_device_resume(struct usb_device *udev)
{
	struct uhci_softc *sc = UHCI_BUS2SC(udev->bus);
	struct usb_xfer *xfer;
	const struct usb_pipe_methods *methods;
	uhci_qh_t *qh;

	DPRINTF("\n");

	USB_BUS_LOCK(udev->bus);

	TAILQ_FOREACH(xfer, &sc->sc_bus.intr_q.head, wait_entry) {

		if (xfer->xroot->udev == udev) {

			methods = xfer->endpoint->methods;
			qh = xfer->qh_start[xfer->flags_int.curr_dma_set];

			if (methods == &uhci_device_bulk_methods) {
				UHCI_APPEND_QH(qh, sc->sc_bulk_p_last);
				uhci_add_loop(sc);
				xfer->flags_int.bandwidth_reclaimed = 1;
			}
			if (methods == &uhci_device_ctrl_methods) {
				if (xfer->xroot->udev->speed == USB_SPEED_LOW) {
					UHCI_APPEND_QH(qh, sc->sc_ls_ctl_p_last);
				} else {
					UHCI_APPEND_QH(qh, sc->sc_fs_ctl_p_last);
				}
			}
			if (methods == &uhci_device_intr_methods) {
				UHCI_APPEND_QH(qh, sc->sc_intr_p_last[xfer->qh_pos]);
			}
		}
	}

	USB_BUS_UNLOCK(udev->bus);

	return;
}

static void
uhci_device_suspend(struct usb_device *udev)
{
	struct uhci_softc *sc = UHCI_BUS2SC(udev->bus);
	struct usb_xfer *xfer;
	const struct usb_pipe_methods *methods;
	uhci_qh_t *qh;

	DPRINTF("\n");

	USB_BUS_LOCK(udev->bus);

	TAILQ_FOREACH(xfer, &sc->sc_bus.intr_q.head, wait_entry) {

		if (xfer->xroot->udev == udev) {

			methods = xfer->endpoint->methods;
			qh = xfer->qh_start[xfer->flags_int.curr_dma_set];

			if (xfer->flags_int.bandwidth_reclaimed) {
				xfer->flags_int.bandwidth_reclaimed = 0;
				uhci_rem_loop(sc);
			}
			if (methods == &uhci_device_bulk_methods) {
				UHCI_REMOVE_QH(qh, sc->sc_bulk_p_last);
			}
			if (methods == &uhci_device_ctrl_methods) {
				if (xfer->xroot->udev->speed == USB_SPEED_LOW) {
					UHCI_REMOVE_QH(qh, sc->sc_ls_ctl_p_last);
				} else {
					UHCI_REMOVE_QH(qh, sc->sc_fs_ctl_p_last);
				}
			}
			if (methods == &uhci_device_intr_methods) {
				UHCI_REMOVE_QH(qh, sc->sc_intr_p_last[xfer->qh_pos]);
			}
		}
	}

	USB_BUS_UNLOCK(udev->bus);

	return;
}

static void
uhci_set_hw_power_sleep(struct usb_bus *bus, uint32_t state)
{
	struct uhci_softc *sc = UHCI_BUS2SC(bus);

	switch (state) {
	case USB_HW_POWER_SUSPEND:
	case USB_HW_POWER_SHUTDOWN:
		uhci_suspend(sc);
		break;
	case USB_HW_POWER_RESUME:
		uhci_resume(sc);
		break;
	default:
		break;
	}
}

static void
uhci_set_hw_power(struct usb_bus *bus)
{
	struct uhci_softc *sc = UHCI_BUS2SC(bus);
	uint32_t flags;

	DPRINTF("\n");

	USB_BUS_LOCK(bus);

	flags = bus->hw_power_state;

	/*
	 * WARNING: Some FULL speed USB devices require periodic SOF
	 * messages! If any USB devices are connected through the
	 * UHCI, power save will be disabled!
	 */
	if (flags & (USB_HW_POWER_CONTROL |
	    USB_HW_POWER_NON_ROOT_HUB |
	    USB_HW_POWER_BULK |
	    USB_HW_POWER_INTERRUPT |
	    USB_HW_POWER_ISOC)) {
		DPRINTF("Some USB transfer is "
		    "active on unit %u.\n",
		    device_get_unit(sc->sc_bus.bdev));
		uhci_restart(sc);
	} else {
		DPRINTF("Power save on unit %u.\n",
		    device_get_unit(sc->sc_bus.bdev));
		UHCICMD(sc, UHCI_CMD_MAXP);
	}

	USB_BUS_UNLOCK(bus);

	return;
}


static const struct usb_bus_methods uhci_bus_methods =
{
	.endpoint_init = uhci_ep_init,
	.xfer_setup = uhci_xfer_setup,
	.xfer_unsetup = uhci_xfer_unsetup,
	.get_dma_delay = uhci_get_dma_delay,
	.device_resume = uhci_device_resume,
	.device_suspend = uhci_device_suspend,
	.set_hw_power = uhci_set_hw_power,
	.set_hw_power_sleep = uhci_set_hw_power_sleep,
	.roothub_exec = uhci_roothub_exec,
	.xfer_poll = uhci_do_poll,
};
