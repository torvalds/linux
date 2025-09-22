/*	$OpenBSD: ehci.c,v 1.222 2024/10/11 09:55:24 kettenis Exp $ */
/*	$NetBSD: ehci.c,v 1.66 2004/06/30 03:11:56 mycroft Exp $	*/

/*
 * Copyright (c) 2014-2015 Martin Pieuchot
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Copyright (c) 2004-2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net), Charles M. Hannum and
 * Jeremy Morse (jeremy.morse@gmail.com).
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

/*
 * TODO:
 * 1) The hub driver needs to handle and schedule the transaction translator,
 *    to assign place in frame where different devices get to go. See chapter
 *    on hubs in USB 2.0 for details.
 *
 * 2) Command failures are not recovered correctly.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/rwlock.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/timeout.h>
#include <sys/pool.h>
#include <sys/endian.h>
#include <sys/atomic.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#include <dev/usb/ehcireg.h>
#include <dev/usb/ehcivar.h>

struct cfdriver ehci_cd = {
	NULL, "ehci", DV_DULL, CD_SKIPHIBERNATE
};

#ifdef EHCI_DEBUG
#define DPRINTF(x)	do { if (ehcidebug) printf x; } while(0)
#define DPRINTFN(n,x)	do { if (ehcidebug>(n)) printf x; } while (0)
int ehcidebug = 0;
#define bitmask_snprintf(q,f,b,l) snprintf((b), (l), "%b", (q), (f))
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

struct pool *ehcixfer;

struct ehci_pipe {
	struct usbd_pipe pipe;

	struct ehci_soft_qh *sqh;
	union {
		/* Control pipe */
		struct {
			struct usb_dma reqdma;
		} ctl;
		/* Iso pipe */
		struct {
			u_int next_frame;
			u_int cur_xfers;
		} isoc;
	} u;
};

u_int8_t		ehci_reverse_bits(u_int8_t, int);

usbd_status	ehci_open(struct usbd_pipe *);
int		ehci_setaddr(struct usbd_device *, int);
void		ehci_poll(struct usbd_bus *);
void		ehci_softintr(void *);
int		ehci_intr1(struct ehci_softc *);
void		ehci_check_intr(struct ehci_softc *, struct usbd_xfer *);
void		ehci_check_qh_intr(struct ehci_softc *, struct usbd_xfer *);
void		ehci_check_itd_intr(struct ehci_softc *, struct usbd_xfer *);
void		ehci_idone(struct usbd_xfer *);
void		ehci_isoc_idone(struct usbd_xfer *);
void		ehci_timeout(void *);
void		ehci_timeout_task(void *);
void		ehci_intrlist_timeout(void *);

struct usbd_xfer *ehci_allocx(struct usbd_bus *);
void		ehci_freex(struct usbd_bus *, struct usbd_xfer *);

usbd_status	ehci_root_ctrl_transfer(struct usbd_xfer *);
usbd_status	ehci_root_ctrl_start(struct usbd_xfer *);
void		ehci_root_ctrl_abort(struct usbd_xfer *);
void		ehci_root_ctrl_close(struct usbd_pipe *);
void		ehci_root_ctrl_done(struct usbd_xfer *);

usbd_status	ehci_root_intr_transfer(struct usbd_xfer *);
usbd_status	ehci_root_intr_start(struct usbd_xfer *);
void		ehci_root_intr_abort(struct usbd_xfer *);
void		ehci_root_intr_close(struct usbd_pipe *);
void		ehci_root_intr_done(struct usbd_xfer *);

usbd_status	ehci_device_ctrl_transfer(struct usbd_xfer *);
usbd_status	ehci_device_ctrl_start(struct usbd_xfer *);
void		ehci_device_ctrl_abort(struct usbd_xfer *);
void		ehci_device_ctrl_close(struct usbd_pipe *);
void		ehci_device_ctrl_done(struct usbd_xfer *);

usbd_status	ehci_device_bulk_transfer(struct usbd_xfer *);
usbd_status	ehci_device_bulk_start(struct usbd_xfer *);
void		ehci_device_bulk_abort(struct usbd_xfer *);
void		ehci_device_bulk_close(struct usbd_pipe *);
void		ehci_device_bulk_done(struct usbd_xfer *);

usbd_status	ehci_device_intr_transfer(struct usbd_xfer *);
usbd_status	ehci_device_intr_start(struct usbd_xfer *);
void		ehci_device_intr_abort(struct usbd_xfer *);
void		ehci_device_intr_close(struct usbd_pipe *);
void		ehci_device_intr_done(struct usbd_xfer *);

usbd_status	ehci_device_isoc_transfer(struct usbd_xfer *);
usbd_status	ehci_device_isoc_start(struct usbd_xfer *);
void		ehci_device_isoc_abort(struct usbd_xfer *);
void		ehci_device_isoc_close(struct usbd_pipe *);
void		ehci_device_isoc_done(struct usbd_xfer *);

void		ehci_device_clear_toggle(struct usbd_pipe *pipe);

void		ehci_pcd(struct ehci_softc *, struct usbd_xfer *);
void		ehci_disown(struct ehci_softc *, int, int);

struct ehci_soft_qh *ehci_alloc_sqh(struct ehci_softc *);
void		ehci_free_sqh(struct ehci_softc *, struct ehci_soft_qh *);

struct ehci_soft_qtd *ehci_alloc_sqtd(struct ehci_softc *);
void		ehci_free_sqtd(struct ehci_softc *, struct ehci_soft_qtd *);
usbd_status	ehci_alloc_sqtd_chain(struct ehci_softc *, u_int,
		    struct usbd_xfer *, struct ehci_soft_qtd **, struct ehci_soft_qtd **);
void		ehci_free_sqtd_chain(struct ehci_softc *, struct ehci_xfer *);

struct ehci_soft_itd *ehci_alloc_itd(struct ehci_softc *);
void		ehci_free_itd(struct ehci_softc *, struct ehci_soft_itd *);
void		ehci_rem_itd_chain(struct ehci_softc *, struct ehci_xfer *);
void		ehci_free_itd_chain(struct ehci_softc *, struct ehci_xfer *);
int		ehci_alloc_itd_chain(struct ehci_softc *, struct usbd_xfer *);
int		ehci_alloc_sitd_chain(struct ehci_softc *, struct usbd_xfer *);
void		ehci_abort_isoc_xfer(struct usbd_xfer *xfer,
		    usbd_status status);

usbd_status	ehci_device_setintr(struct ehci_softc *, struct ehci_soft_qh *,
			    int ival);

void		ehci_add_qh(struct ehci_soft_qh *, struct ehci_soft_qh *);
void		ehci_rem_qh(struct ehci_softc *, struct ehci_soft_qh *);
void		ehci_set_qh_qtd(struct ehci_soft_qh *, struct ehci_soft_qtd *);
void		ehci_sync_hc(struct ehci_softc *);

void		ehci_close_pipe(struct usbd_pipe *);
void		ehci_abort_xfer(struct usbd_xfer *, usbd_status);

#ifdef EHCI_DEBUG
void		ehci_dump_regs(struct ehci_softc *);
void		ehci_dump(void);
struct ehci_softc *theehci;
void		ehci_dump_link(ehci_link_t, int);
void		ehci_dump_sqtds(struct ehci_soft_qtd *);
void		ehci_dump_sqtd(struct ehci_soft_qtd *);
void		ehci_dump_qtd(struct ehci_qtd *);
void		ehci_dump_sqh(struct ehci_soft_qh *);
#if notyet
void		ehci_dump_itd(struct ehci_soft_itd *);
#endif
#ifdef DIAGNOSTIC
void		ehci_dump_exfer(struct ehci_xfer *);
#endif
#endif

#define EHCI_INTR_ENDPT 1

const struct usbd_bus_methods ehci_bus_methods = {
	.open_pipe = ehci_open,
	.dev_setaddr = ehci_setaddr,
	.soft_intr = ehci_softintr,
	.do_poll = ehci_poll,
	.allocx = ehci_allocx,
	.freex = ehci_freex,
};

const struct usbd_pipe_methods ehci_root_ctrl_methods = {
	.transfer = ehci_root_ctrl_transfer,
	.start = ehci_root_ctrl_start,
	.abort = ehci_root_ctrl_abort,
	.close = ehci_root_ctrl_close,
	.done = ehci_root_ctrl_done,
};

const struct usbd_pipe_methods ehci_root_intr_methods = {
	.transfer = ehci_root_intr_transfer,
	.start = ehci_root_intr_start,
	.abort = ehci_root_intr_abort,
	.close = ehci_root_intr_close,
	.done = ehci_root_intr_done,
};

const struct usbd_pipe_methods ehci_device_ctrl_methods = {
	.transfer = ehci_device_ctrl_transfer,
	.start = ehci_device_ctrl_start,
	.abort = ehci_device_ctrl_abort,
	.close = ehci_device_ctrl_close,
	.done = ehci_device_ctrl_done,
};

const struct usbd_pipe_methods ehci_device_intr_methods = {
	.transfer = ehci_device_intr_transfer,
	.start = ehci_device_intr_start,
	.abort = ehci_device_intr_abort,
	.close = ehci_device_intr_close,
	.cleartoggle = ehci_device_clear_toggle,
	.done = ehci_device_intr_done,
};

const struct usbd_pipe_methods ehci_device_bulk_methods = {
	.transfer = ehci_device_bulk_transfer,
	.start = ehci_device_bulk_start,
	.abort = ehci_device_bulk_abort,
	.close = ehci_device_bulk_close,
	.cleartoggle = ehci_device_clear_toggle,
	.done = ehci_device_bulk_done,
};

const struct usbd_pipe_methods ehci_device_isoc_methods = {
	.transfer = ehci_device_isoc_transfer,
	.start = ehci_device_isoc_start,
	.abort = ehci_device_isoc_abort,
	.close = ehci_device_isoc_close,
	.done = ehci_device_isoc_done,
};

/*
 * Reverse a number with nbits bits.  Used to evenly distribute lower-level
 * interrupt heads in the periodic schedule.
 * Suitable for use with EHCI_IPOLLRATES <= 9.
 */
u_int8_t
ehci_reverse_bits(u_int8_t c, int nbits)
{
	c = ((c >> 1) & 0x55) | ((c << 1) & 0xaa);
	c = ((c >> 2) & 0x33) | ((c << 2) & 0xcc);
	c = ((c >> 4) & 0x0f) | ((c << 4) & 0xf0);

	return c >> (8 - nbits);
}

usbd_status
ehci_init(struct ehci_softc *sc)
{
	u_int32_t sparams, cparams, hcr;
	u_int i, j;
	usbd_status err;
	struct ehci_soft_qh *sqh;

#ifdef EHCI_DEBUG
	u_int32_t vers;
	theehci = sc;

	DPRINTF(("ehci_init: start\n"));

	vers = EREAD2(sc, EHCI_HCIVERSION);
	DPRINTF(("%s: EHCI version %x.%x\n", sc->sc_bus.bdev.dv_xname,
	    vers >> 8, vers & 0xff));
#endif

	sc->sc_offs = EREAD1(sc, EHCI_CAPLENGTH);

	sparams = EREAD4(sc, EHCI_HCSPARAMS);
	DPRINTF(("ehci_init: sparams=0x%x\n", sparams));
	sc->sc_noport = EHCI_HCS_N_PORTS(sparams);
	cparams = EREAD4(sc, EHCI_HCCPARAMS);
	DPRINTF(("ehci_init: cparams=0x%x\n", cparams));

	/* MUST clear segment register if 64 bit capable. */
	if (EHCI_HCC_64BIT(cparams))
		EWRITE4(sc, EHCI_CTRLDSSEGMENT, 0);

	sc->sc_bus.usbrev = USBREV_2_0;

	DPRINTF(("%s: resetting\n", sc->sc_bus.bdev.dv_xname));
	err = ehci_reset(sc);
	if (err)
		return (err);

	if (ehcixfer == NULL) {
		ehcixfer = malloc(sizeof(struct pool), M_USBHC, M_NOWAIT);
		if (ehcixfer == NULL) {
			printf("%s: unable to allocate pool descriptor\n",
			    sc->sc_bus.bdev.dv_xname);
			return (ENOMEM);
		}
		pool_init(ehcixfer, sizeof(struct ehci_xfer), 0, IPL_SOFTUSB,
		    0, "ehcixfer", NULL);
	}

	/* frame list size at default, read back what we got and use that */
	switch (EHCI_CMD_FLS(EOREAD4(sc, EHCI_USBCMD))) {
	case 0:
		sc->sc_flsize = 1024;
		break;
	case 1:
		sc->sc_flsize = 512;
		break;
	case 2:
		sc->sc_flsize = 256;
		break;
	case 3:
		return (USBD_IOERROR);
	}
	err = usb_allocmem(&sc->sc_bus, sc->sc_flsize * sizeof(ehci_link_t),
	    EHCI_FLALIGN_ALIGN, USB_DMA_COHERENT, &sc->sc_fldma);
	if (err)
		return (err);
	DPRINTF(("%s: flsize=%d\n", sc->sc_bus.bdev.dv_xname,sc->sc_flsize));
	sc->sc_flist = KERNADDR(&sc->sc_fldma, 0);

	for (i = 0; i < sc->sc_flsize; i++)
		sc->sc_flist[i] = htole32(EHCI_LINK_TERMINATE);

	EOWRITE4(sc, EHCI_PERIODICLISTBASE, DMAADDR(&sc->sc_fldma, 0));

	sc->sc_softitds = mallocarray(sc->sc_flsize,
	    sizeof(struct ehci_soft_itd *), M_USBHC, M_NOWAIT | M_ZERO);
	if (sc->sc_softitds == NULL) {
		usb_freemem(&sc->sc_bus, &sc->sc_fldma);
		return (ENOMEM);
	}
	LIST_INIT(&sc->sc_freeitds);
	TAILQ_INIT(&sc->sc_intrhead);

	/* Set up the bus struct. */
	sc->sc_bus.methods = &ehci_bus_methods;
	sc->sc_bus.pipe_size = sizeof(struct ehci_pipe);

	sc->sc_eintrs = EHCI_NORMAL_INTRS;

	/*
	 * Allocate the interrupt dummy QHs. These are arranged to give poll
	 * intervals that are powers of 2 times 1ms.
	 */
	for (i = 0; i < EHCI_INTRQHS; i++) {
		sqh = ehci_alloc_sqh(sc);
		if (sqh == NULL) {
			err = USBD_NOMEM;
			goto bad1;
		}
		sc->sc_islots[i].sqh = sqh;
	}
	for (i = 0; i < EHCI_INTRQHS; i++) {
		sqh = sc->sc_islots[i].sqh;
		if (i == 0) {
			/* The last (1ms) QH terminates. */
			sqh->qh.qh_link = htole32(EHCI_LINK_TERMINATE);
			sqh->next = NULL;
		} else {
			/* Otherwise the next QH has half the poll interval */
			sqh->next = sc->sc_islots[(i + 1) / 2 - 1].sqh;
			sqh->qh.qh_link = htole32(sqh->next->physaddr |
			    EHCI_LINK_QH);
		}
		sqh->qh.qh_endp = htole32(EHCI_QH_SET_EPS(EHCI_QH_SPEED_HIGH));
		sqh->qh.qh_endphub = htole32(EHCI_QH_SET_MULT(1));
		sqh->qh.qh_curqtd = htole32(EHCI_LINK_TERMINATE);
		sqh->qh.qh_qtd.qtd_next = htole32(EHCI_LINK_TERMINATE);
		sqh->qh.qh_qtd.qtd_altnext = htole32(EHCI_LINK_TERMINATE);
		sqh->qh.qh_qtd.qtd_status = htole32(EHCI_QTD_HALTED);
		sqh->sqtd = NULL;
		usb_syncmem(&sqh->dma, sqh->offs, sizeof(sqh->qh),
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	}
	/* Point the frame list at the last level (128ms). */
	for (i = 0; i < (1 << (EHCI_IPOLLRATES - 1)); i++)
		for (j = i; j < sc->sc_flsize; j += 1 << (EHCI_IPOLLRATES - 1))
			sc->sc_flist[j] = htole32(EHCI_LINK_QH | sc->sc_islots[
			    EHCI_IQHIDX(EHCI_IPOLLRATES - 1, ehci_reverse_bits(
			    i, EHCI_IPOLLRATES - 1))].sqh->physaddr);
	usb_syncmem(&sc->sc_fldma, 0, sc->sc_flsize * sizeof(ehci_link_t),
	    BUS_DMASYNC_PREWRITE);

	/* Allocate dummy QH that starts the async list. */
	sqh = ehci_alloc_sqh(sc);
	if (sqh == NULL) {
		err = USBD_NOMEM;
		goto bad1;
	}
	/* Fill the QH */
	sqh->qh.qh_endp =
	    htole32(EHCI_QH_SET_EPS(EHCI_QH_SPEED_HIGH) | EHCI_QH_HRECL);
	sqh->qh.qh_link =
	    htole32(sqh->physaddr | EHCI_LINK_QH);
	sqh->qh.qh_curqtd = htole32(EHCI_LINK_TERMINATE);
	sqh->prev = sqh; /*It's a circular list.. */
	sqh->next = sqh;
	/* Fill the overlay qTD */
	sqh->qh.qh_qtd.qtd_next = htole32(EHCI_LINK_TERMINATE);
	sqh->qh.qh_qtd.qtd_altnext = htole32(EHCI_LINK_TERMINATE);
	sqh->qh.qh_qtd.qtd_status = htole32(EHCI_QTD_HALTED);
	sqh->sqtd = NULL;
	usb_syncmem(&sqh->dma, sqh->offs, sizeof(sqh->qh),
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	/* Point to async list */
	sc->sc_async_head = sqh;
	EOWRITE4(sc, EHCI_ASYNCLISTADDR, sqh->physaddr | EHCI_LINK_QH);

	timeout_set(&sc->sc_tmo_intrlist, ehci_intrlist_timeout, sc);

	rw_init(&sc->sc_doorbell_lock, "ehcidb");

	/* Turn on controller */
	EOWRITE4(sc, EHCI_USBCMD,
	    EHCI_CMD_ITC_2 | /* 2 microframes interrupt delay */
	    (EOREAD4(sc, EHCI_USBCMD) & EHCI_CMD_FLS_M) |
	    EHCI_CMD_ASE |
	    EHCI_CMD_PSE |
	    EHCI_CMD_RS);

	/* Take over port ownership */
	EOWRITE4(sc, EHCI_CONFIGFLAG, EHCI_CONF_CF);

	for (i = 0; i < 100; i++) {
		usb_delay_ms(&sc->sc_bus, 1);
		hcr = EOREAD4(sc, EHCI_USBSTS) & EHCI_STS_HCH;
		if (!hcr)
			break;
	}
	if (hcr) {
		printf("%s: run timeout\n", sc->sc_bus.bdev.dv_xname);
		return (USBD_IOERROR);
	}

	/* Enable interrupts */
	EOWRITE4(sc, EHCI_USBINTR, sc->sc_eintrs);

	return (USBD_NORMAL_COMPLETION);

#if 0
 bad2:
	ehci_free_sqh(sc, sc->sc_async_head);
#endif
 bad1:
	free(sc->sc_softitds, M_USBHC,
	    sc->sc_flsize * sizeof(struct ehci_soft_itd *));
	usb_freemem(&sc->sc_bus, &sc->sc_fldma);
	return (err);
}

int
ehci_intr(void *v)
{
	struct ehci_softc *sc = v;

	if (sc == NULL || sc->sc_bus.dying)
		return (0);

	/* If we get an interrupt while polling, then just ignore it. */
	if (sc->sc_bus.use_polling) {
		u_int32_t intrs = EHCI_STS_INTRS(EOREAD4(sc, EHCI_USBSTS));

		if (intrs)
			EOWRITE4(sc, EHCI_USBSTS, intrs); /* Acknowledge */
		return (0);
	}

	return (ehci_intr1(sc));
}

int
ehci_intr1(struct ehci_softc *sc)
{
	u_int32_t intrs, eintrs;

	/* In case the interrupt occurs before initialization has completed. */
	if (sc == NULL) {
#ifdef DIAGNOSTIC
		printf("ehci_intr1: sc == NULL\n");
#endif
		return (0);
	}

	intrs = EOREAD4(sc, EHCI_USBSTS);
	if (intrs == 0xffffffff) {
		sc->sc_bus.dying = 1;
		return (0);
	}
	intrs = EHCI_STS_INTRS(intrs);
	if (!intrs)
		return (0);

	eintrs = intrs & sc->sc_eintrs;
	if (!eintrs)
		return (0);

	EOWRITE4(sc, EHCI_USBSTS, intrs); /* Acknowledge */
	sc->sc_bus.no_intrs++;

	if (eintrs & EHCI_STS_HSE) {
		printf("%s: unrecoverable error, controller halted\n",
		       sc->sc_bus.bdev.dv_xname);
		sc->sc_bus.dying = 1;
		return (1);
	}
	if (eintrs & EHCI_STS_IAA) {
		wakeup(&sc->sc_async_head);
		eintrs &= ~EHCI_STS_IAA;
	}
	if (eintrs & (EHCI_STS_INT | EHCI_STS_ERRINT)) {
		usb_schedsoftintr(&sc->sc_bus);
		eintrs &= ~(EHCI_STS_INT | EHCI_STS_ERRINT);
	}
	if (eintrs & EHCI_STS_PCD) {
		atomic_setbits_int(&sc->sc_flags, EHCIF_PCB_INTR);
		usb_schedsoftintr(&sc->sc_bus);
		eintrs &= ~EHCI_STS_PCD;
	}

	if (eintrs != 0) {
		/* Block unprocessed interrupts. */
		sc->sc_eintrs &= ~eintrs;
		EOWRITE4(sc, EHCI_USBINTR, sc->sc_eintrs);
		printf("%s: blocking intrs 0x%x\n",
		       sc->sc_bus.bdev.dv_xname, eintrs);
	}

	return (1);
}

void
ehci_pcd(struct ehci_softc *sc, struct usbd_xfer *xfer)
{
	u_char *p;
	int i, m;

	if (xfer == NULL) {
		/* Just ignore the change. */
		return;
	}

	p = KERNADDR(&xfer->dmabuf, 0);
	m = min(sc->sc_noport, xfer->length * 8 - 1);
	memset(p, 0, xfer->length);
	for (i = 1; i <= m; i++) {
		/* Pick out CHANGE bits from the status reg. */
		if (EOREAD4(sc, EHCI_PORTSC(i)) & EHCI_PS_CLEAR)
			p[i / 8] |= 1 << (i % 8);
	}
	xfer->actlen = xfer->length;
	xfer->status = USBD_NORMAL_COMPLETION;

	usb_transfer_complete(xfer);
}

/*
 * Work around the half configured control (default) pipe when setting
 * the address of a device.
 *
 * Because a single QH is setup per endpoint in ehci_open(), and the
 * control pipe is configured before we could have set the address
 * of the device or read the wMaxPacketSize of the endpoint, we have
 * to re-open the pipe twice here.
 */
int
ehci_setaddr(struct usbd_device *dev, int addr)
{
	/* Root Hub */
	if (dev->depth == 0)
		return (0);

	/* Re-establish the default pipe with the new max packet size. */
	ehci_close_pipe(dev->default_pipe);
	if (ehci_open(dev->default_pipe))
		return (EINVAL);

	if (usbd_set_address(dev, addr))
		return (1);

	dev->address = addr;

	/* Re-establish the default pipe with the new address. */
	ehci_close_pipe(dev->default_pipe);
	if (ehci_open(dev->default_pipe))
		return (EINVAL);

	return (0);
}

void
ehci_softintr(void *v)
{
	struct ehci_softc *sc = v;
	struct ehci_xfer *ex, *nextex;

	if (sc->sc_bus.dying)
		return;

	sc->sc_bus.intr_context++;

	if (sc->sc_flags & EHCIF_PCB_INTR) {
		atomic_clearbits_int(&sc->sc_flags, EHCIF_PCB_INTR);
		ehci_pcd(sc, sc->sc_intrxfer);
	}

	/*
	 * The only explanation I can think of for why EHCI is as brain dead
	 * as UHCI interrupt-wise is that Intel was involved in both.
	 * An interrupt just tells us that something is done, we have no
	 * clue what, so we need to scan through all active transfers. :-(
	 */
	for (ex = TAILQ_FIRST(&sc->sc_intrhead); ex; ex = nextex) {
		nextex = TAILQ_NEXT(ex, inext);
		ehci_check_intr(sc, &ex->xfer);
	}

	/* Schedule a callout to catch any dropped transactions. */
	if ((sc->sc_flags & EHCIF_DROPPED_INTR_WORKAROUND) &&
	    !TAILQ_EMPTY(&sc->sc_intrhead)) {
		timeout_add_sec(&sc->sc_tmo_intrlist, 1);
	}

	if (sc->sc_softwake) {
		sc->sc_softwake = 0;
		wakeup(&sc->sc_softwake);
	}

	sc->sc_bus.intr_context--;
}

void
ehci_check_intr(struct ehci_softc *sc, struct usbd_xfer *xfer)
{
	int attr = xfer->pipe->endpoint->edesc->bmAttributes;

	if (UE_GET_XFERTYPE(attr) == UE_ISOCHRONOUS)
		ehci_check_itd_intr(sc, xfer);
	else
		ehci_check_qh_intr(sc, xfer);
}

void
ehci_check_qh_intr(struct ehci_softc *sc, struct usbd_xfer *xfer)
{
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;
	struct ehci_soft_qtd *sqtd, *lsqtd = ex->sqtdend;
	uint32_t status;

	KASSERT(ex->sqtdstart != NULL && ex->sqtdend != NULL);

	usb_syncmem(&lsqtd->dma,
	    lsqtd->offs + offsetof(struct ehci_qtd, qtd_status),
	    sizeof(lsqtd->qtd.qtd_status),
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);

	/*
	 * If the last TD is still active we need to check whether there
	 * is a an error somewhere in the middle, or whether there was a
	 * short packet (SPD and not ACTIVE).
	 */
	if (letoh32(lsqtd->qtd.qtd_status) & EHCI_QTD_ACTIVE) {
		DPRINTFN(12, ("ehci_check_intr: active ex=%p\n", ex));
		for (sqtd = ex->sqtdstart; sqtd != lsqtd; sqtd=sqtd->nextqtd) {
			usb_syncmem(&sqtd->dma,
			    sqtd->offs + offsetof(struct ehci_qtd, qtd_status),
			    sizeof(sqtd->qtd.qtd_status),
			    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
			status = letoh32(sqtd->qtd.qtd_status);
			usb_syncmem(&sqtd->dma,
			    sqtd->offs + offsetof(struct ehci_qtd, qtd_status),
			    sizeof(sqtd->qtd.qtd_status), BUS_DMASYNC_PREREAD);
			/* If there's an active QTD the xfer isn't done. */
			if (status & EHCI_QTD_ACTIVE)
				break;
			/* Any kind of error makes the xfer done. */
			if (status & EHCI_QTD_HALTED)
				goto done;
			/* We want short packets, and it is short: it's done */
			if (EHCI_QTD_GET_BYTES(status) != 0)
				goto done;
		}
		DPRINTFN(12, ("ehci_check_intr: ex=%p std=%p still active\n",
			      ex, ex->sqtdstart));
		usb_syncmem(&lsqtd->dma,
		    lsqtd->offs + offsetof(struct ehci_qtd, qtd_status),
		    sizeof(lsqtd->qtd.qtd_status), BUS_DMASYNC_PREREAD);
		return;
	}
 done:
	TAILQ_REMOVE(&sc->sc_intrhead, ex, inext);
	timeout_del(&xfer->timeout_handle);
	usb_rem_task(xfer->pipe->device, &xfer->abort_task);
	ehci_idone(xfer);
}

void
ehci_check_itd_intr(struct ehci_softc *sc, struct usbd_xfer *xfer)
{
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;
	struct ehci_soft_itd *itd = ex->itdend;
	int i;

	if (xfer != SIMPLEQ_FIRST(&xfer->pipe->queue))
		return;

	KASSERT(ex->itdstart != NULL && ex->itdend != NULL);

	/* Check no active transfers in last itd, meaning we're finished */
	if (xfer->device->speed == USB_SPEED_HIGH) {
		usb_syncmem(&itd->dma,
		    itd->offs + offsetof(struct ehci_itd, itd_ctl),
		    sizeof(itd->itd.itd_ctl),
		    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);

		for (i = 0; i < 8; i++) {
			if (letoh32(itd->itd.itd_ctl[i]) & EHCI_ITD_ACTIVE)
				return;
		}
	} else {
		usb_syncmem(&itd->dma,
		    itd->offs + offsetof(struct ehci_sitd, sitd_trans),
		    sizeof(itd->sitd.sitd_trans),
		    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);

		if (le32toh(itd->sitd.sitd_trans) & EHCI_SITD_ACTIVE)
			return;
	}

	/* All descriptor(s) inactive, it's done */
	TAILQ_REMOVE(&sc->sc_intrhead, ex, inext);
	timeout_del(&xfer->timeout_handle);
	usb_rem_task(xfer->pipe->device, &xfer->abort_task);
	ehci_isoc_idone(xfer);
}

void
ehci_isoc_idone(struct usbd_xfer *xfer)
{
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;
	struct ehci_soft_itd *itd;
	int i, len, uframes, nframes = 0, actlen = 0;
	uint32_t status = 0;

	if (xfer->status == USBD_CANCELLED || xfer->status == USBD_TIMEOUT)
		return;

	if (xfer->device->speed == USB_SPEED_HIGH) {
		switch (xfer->pipe->endpoint->edesc->bInterval) {
		case 0:
			panic("isoc xfer suddenly has 0 bInterval, invalid");
		case 1:
			uframes = 1;
			break;
		case 2:
			uframes = 2;
			break;
		case 3:
			uframes = 4;
			break;
		default:
			uframes = 8;
			break;
		}

		for (itd = ex->itdstart; itd != NULL; itd = itd->xfer_next) {
			usb_syncmem(&itd->dma,
			    itd->offs + offsetof(struct ehci_itd, itd_ctl),
			    sizeof(itd->itd.itd_ctl), BUS_DMASYNC_POSTWRITE |
			    BUS_DMASYNC_POSTREAD);

			for (i = 0; i < 8; i += uframes) {
				/* XXX - driver didn't fill in the frame full
				 *   of uframes. This leads to scheduling
				 *   inefficiencies, but working around
				 *   this doubles complexity of tracking
				 *   an xfer.
				 */
				if (nframes >= xfer->nframes)
					break;

				status = letoh32(itd->itd.itd_ctl[i]);
				len = EHCI_ITD_GET_LEN(status);
				if (EHCI_ITD_GET_STATUS(status) != 0)
					len = 0; /*No valid data on error*/

				xfer->frlengths[nframes++] = len;
				actlen += len;
			}
		}
	} else {
		for (itd = ex->itdstart; itd != NULL; itd = itd->xfer_next) {
			usb_syncmem(&itd->dma,
			    itd->offs + offsetof(struct ehci_sitd, sitd_trans),
			    sizeof(itd->sitd.sitd_trans),
			    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);

			status = le32toh(itd->sitd.sitd_trans);
			len = EHCI_SITD_GET_LEN(status);
			if (xfer->frlengths[nframes] >= len)
				len = xfer->frlengths[nframes] - len;
			else
				len = 0;

			xfer->frlengths[nframes++] = len;
			actlen += len;
	    	}
	}

#ifdef DIAGNOSTIC
	ex->isdone = 1;
#endif
	xfer->actlen = actlen;
	xfer->status = USBD_NORMAL_COMPLETION;

	usb_syncmem(&xfer->dmabuf, 0, xfer->length,
	    usbd_xfer_isread(xfer) ?
	    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
	usb_transfer_complete(xfer);
}

void
ehci_idone(struct usbd_xfer *xfer)
{
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;
	struct ehci_soft_qtd *sqtd;
	u_int32_t status = 0, nstatus = 0;
	int actlen, cerr;

#ifdef DIAGNOSTIC
	{
		int s = splhigh();
		if (ex->isdone) {
			splx(s);
			printf("ehci_idone: ex=%p is done!\n", ex);
			return;
		}
		ex->isdone = 1;
		splx(s);
	}
#endif
	if (xfer->status == USBD_CANCELLED || xfer->status == USBD_TIMEOUT)
		return;

	actlen = 0;
	for (sqtd = ex->sqtdstart; sqtd != NULL; sqtd = sqtd->nextqtd) {
		usb_syncmem(&sqtd->dma, sqtd->offs, sizeof(sqtd->qtd),
		    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
		nstatus = letoh32(sqtd->qtd.qtd_status);
		if (nstatus & EHCI_QTD_ACTIVE)
			break;

		status = nstatus;
		/* halt is ok if descriptor is last, and complete */
		if (sqtd->qtd.qtd_next == htole32(EHCI_LINK_TERMINATE) &&
		    EHCI_QTD_GET_BYTES(status) == 0)
			status &= ~EHCI_QTD_HALTED;
		if (EHCI_QTD_GET_PID(status) !=	EHCI_QTD_PID_SETUP)
			actlen += sqtd->len - EHCI_QTD_GET_BYTES(status);
	}

	cerr = EHCI_QTD_GET_CERR(status);
	DPRINTFN(/*10*/2, ("ehci_idone: len=%d, actlen=%d, cerr=%d, "
	    "status=0x%x\n", xfer->length, actlen, cerr, status));
	xfer->actlen = actlen;
	if ((status & EHCI_QTD_HALTED) != 0) {
		if ((status & EHCI_QTD_BABBLE) == 0 && cerr > 0)
			xfer->status = USBD_STALLED;
		else
			xfer->status = USBD_IOERROR; /* more info XXX */
	} else
		xfer->status = USBD_NORMAL_COMPLETION;

	if (xfer->actlen)
		usb_syncmem(&xfer->dmabuf, 0, xfer->actlen,
		    usbd_xfer_isread(xfer) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
	usb_transfer_complete(xfer);
	DPRINTFN(/*12*/2, ("ehci_idone: ex=%p done\n", ex));
}

void
ehci_poll(struct usbd_bus *bus)
{
	struct ehci_softc *sc = (struct ehci_softc *)bus;

	if (EOREAD4(sc, EHCI_USBSTS) & sc->sc_eintrs)
		ehci_intr1(sc);
}

int
ehci_detach(struct device *self, int flags)
{
	struct ehci_softc *sc = (struct ehci_softc *)self;
	int rv;

	rv = config_detach_children(self, flags);
	if (rv != 0)
		return (rv);

	timeout_del(&sc->sc_tmo_intrlist);

	ehci_reset(sc);

	usb_delay_ms(&sc->sc_bus, 300); /* XXX let stray task complete */

	free(sc->sc_softitds, M_USBHC,
	    sc->sc_flsize * sizeof(struct ehci_soft_itd *));
	usb_freemem(&sc->sc_bus, &sc->sc_fldma);
	/* XXX free other data structures XXX */

	return (rv);
}


int
ehci_activate(struct device *self, int act)
{
	struct ehci_softc *sc = (struct ehci_softc *)self;
	u_int32_t cmd, hcr, cparams;
	int i, rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);

#ifdef DIAGNOSTIC
		if (!TAILQ_EMPTY(&sc->sc_intrhead)) {
			printf("%s: interrupt list not empty\n",
			    sc->sc_bus.bdev.dv_xname);
			return (-1);
		}
#endif

		sc->sc_bus.use_polling++;

		for (i = 1; i <= sc->sc_noport; i++) {
			cmd = EOREAD4(sc, EHCI_PORTSC(i));
			if ((cmd & (EHCI_PS_PO|EHCI_PS_PE)) == EHCI_PS_PE)
				EOWRITE4(sc, EHCI_PORTSC(i),
				    cmd | EHCI_PS_SUSP);
		}

		/*
		 * First tell the host to stop processing Asynchronous
		 * and Periodic schedules.
		 */
		cmd = EOREAD4(sc, EHCI_USBCMD) & ~(EHCI_CMD_ASE | EHCI_CMD_PSE);
		EOWRITE4(sc, EHCI_USBCMD, cmd);
		for (i = 0; i < 100; i++) {
			usb_delay_ms(&sc->sc_bus, 1);
			hcr = EOREAD4(sc, EHCI_USBSTS) &
			    (EHCI_STS_ASS | EHCI_STS_PSS);
			if (hcr == 0)
				break;
		}
		if (hcr != 0)
			printf("%s: disable schedules timeout\n",
			    sc->sc_bus.bdev.dv_xname);

		/*
		 * Then reset the host as if it was a shutdown.
		 *
		 * All USB devices are disconnected/reconnected during
		 * a suspend/resume cycle so keep it simple.
		 */
		ehci_reset(sc);

		sc->sc_bus.use_polling--;
		break;
	case DVACT_RESUME:
		sc->sc_bus.use_polling++;

		ehci_reset(sc);

		cparams = EREAD4(sc, EHCI_HCCPARAMS);
		/* MUST clear segment register if 64 bit capable. */
		if (EHCI_HCC_64BIT(cparams))
			EWRITE4(sc, EHCI_CTRLDSSEGMENT, 0);

		EOWRITE4(sc, EHCI_PERIODICLISTBASE, DMAADDR(&sc->sc_fldma, 0));
		EOWRITE4(sc, EHCI_ASYNCLISTADDR,
	  	    sc->sc_async_head->physaddr | EHCI_LINK_QH);

		hcr = 0;
		for (i = 1; i <= sc->sc_noport; i++) {
			cmd = EOREAD4(sc, EHCI_PORTSC(i));
			if ((cmd & (EHCI_PS_PO|EHCI_PS_SUSP)) == EHCI_PS_SUSP) {
				EOWRITE4(sc, EHCI_PORTSC(i), cmd | EHCI_PS_FPR);
				hcr = 1;
			}
		}

		if (hcr) {
			usb_delay_ms(&sc->sc_bus, USB_RESUME_WAIT);
			for (i = 1; i <= sc->sc_noport; i++) {
				cmd = EOREAD4(sc, EHCI_PORTSC(i));
				if ((cmd & (EHCI_PS_PO|EHCI_PS_SUSP)) ==
				   EHCI_PS_SUSP)
					EOWRITE4(sc, EHCI_PORTSC(i),
					   cmd & ~EHCI_PS_FPR);
			}
		}

		/* Turn on controller */
		EOWRITE4(sc, EHCI_USBCMD,
		    EHCI_CMD_ITC_2 | /* 2 microframes interrupt delay */
		    (EOREAD4(sc, EHCI_USBCMD) & EHCI_CMD_FLS_M) |
		    EHCI_CMD_ASE |
		    EHCI_CMD_PSE |
		    EHCI_CMD_RS);

		/* Take over port ownership */
		EOWRITE4(sc, EHCI_CONFIGFLAG, EHCI_CONF_CF);
		for (i = 0; i < 100; i++) {
			usb_delay_ms(&sc->sc_bus, 1);
			hcr = EOREAD4(sc, EHCI_USBSTS) & EHCI_STS_HCH;
			if (!hcr)
				break;
		}

		if (hcr) {
			printf("%s: run timeout\n", sc->sc_bus.bdev.dv_xname);
			/* XXX should we bail here? */
		}

		EOWRITE4(sc, EHCI_USBINTR, sc->sc_eintrs);

		usb_delay_ms(&sc->sc_bus, USB_RESUME_WAIT);

		sc->sc_bus.use_polling--;
		rv = config_activate_children(self, act);
		break;
	case DVACT_POWERDOWN:
		rv = config_activate_children(self, act);
		ehci_reset(sc);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

usbd_status
ehci_reset(struct ehci_softc *sc)
{
	u_int32_t hcr, usbmode;
	int i;

	EOWRITE4(sc, EHCI_USBCMD, 0);	/* Halt controller */
	for (i = 0; i < 100; i++) {
		usb_delay_ms(&sc->sc_bus, 1);
		hcr = EOREAD4(sc, EHCI_USBSTS) & EHCI_STS_HCH;
		if (hcr)
			break;
	}

	if (!hcr)
		printf("%s: halt timeout\n", sc->sc_bus.bdev.dv_xname);

	if (sc->sc_flags & EHCIF_USBMODE)
		usbmode = EOREAD4(sc, EHCI_USBMODE);

	EOWRITE4(sc, EHCI_USBCMD, EHCI_CMD_HCRESET);
	for (i = 0; i < 100; i++) {
		usb_delay_ms(&sc->sc_bus, 1);
		hcr = EOREAD4(sc, EHCI_USBCMD) & EHCI_CMD_HCRESET;
		if (!hcr)
			break;
	}

	if (hcr) {
		printf("%s: reset timeout\n", sc->sc_bus.bdev.dv_xname);
		return (USBD_IOERROR);
	}

	if (sc->sc_flags & EHCIF_USBMODE)
		EOWRITE4(sc, EHCI_USBMODE, usbmode);

	return (USBD_NORMAL_COMPLETION);
}

struct usbd_xfer *
ehci_allocx(struct usbd_bus *bus)
{
	struct ehci_xfer *ex;

	ex = pool_get(ehcixfer, PR_NOWAIT | PR_ZERO);
#ifdef DIAGNOSTIC
	if (ex != NULL)
		ex->isdone = 1;
#endif
	return ((struct usbd_xfer *)ex);
}

void
ehci_freex(struct usbd_bus *bus, struct usbd_xfer *xfer)
{
	struct ehci_xfer *ex = (struct ehci_xfer*)xfer;

#ifdef DIAGNOSTIC
	if (!ex->isdone) {
		printf("%s: !isdone\n", __func__);
		return;
	}
#endif
	pool_put(ehcixfer, ex);
}

void
ehci_device_clear_toggle(struct usbd_pipe *pipe)
{
	struct ehci_pipe *epipe = (struct ehci_pipe *)pipe;

#ifdef DIAGNOSTIC
	if ((epipe->sqh->qh.qh_qtd.qtd_status & htole32(EHCI_QTD_ACTIVE)) != 0)
		printf("%s: queue active\n", __func__);
#endif
	epipe->sqh->qh.qh_qtd.qtd_status &= htole32(~EHCI_QTD_TOGGLE_MASK);
}

#ifdef EHCI_DEBUG
void
ehci_dump_regs(struct ehci_softc *sc)
{
	int i;

	printf("cmd=0x%08x, sts=0x%08x, ien=0x%08x\n",
	    EOREAD4(sc, EHCI_USBCMD),
	    EOREAD4(sc, EHCI_USBSTS),
	    EOREAD4(sc, EHCI_USBINTR));
	printf("frindex=0x%08x ctrdsegm=0x%08x periodic=0x%08x async=0x%08x\n",
	    EOREAD4(sc, EHCI_FRINDEX),
	    EOREAD4(sc, EHCI_CTRLDSSEGMENT),
	    EOREAD4(sc, EHCI_PERIODICLISTBASE),
	    EOREAD4(sc, EHCI_ASYNCLISTADDR));
	for (i = 1; i <= sc->sc_noport; i++)
		printf("port %d status=0x%08x\n", i,
		    EOREAD4(sc, EHCI_PORTSC(i)));
}

/*
 * Unused function - this is meant to be called from a kernel
 * debugger.
 */
void
ehci_dump(void)
{
	ehci_dump_regs(theehci);
}

void
ehci_dump_link(ehci_link_t link, int type)
{
	link = letoh32(link);
	printf("0x%08x", link);
	if (link & EHCI_LINK_TERMINATE)
		printf("<T>");
	else {
		printf("<");
		if (type) {
			switch (EHCI_LINK_TYPE(link)) {
			case EHCI_LINK_ITD:
				printf("ITD");
				break;
			case EHCI_LINK_QH:
				printf("QH");
				break;
			case EHCI_LINK_SITD:
				printf("SITD");
				break;
			case EHCI_LINK_FSTN:
				printf("FSTN");
				break;
			}
		}
		printf(">");
	}
}

void
ehci_dump_sqtds(struct ehci_soft_qtd *sqtd)
{
	int i;
	u_int32_t stop;

	stop = 0;
	for (i = 0; sqtd && i < 20 && !stop; sqtd = sqtd->nextqtd, i++) {
		ehci_dump_sqtd(sqtd);
		usb_syncmem(&sqtd->dma,
		    sqtd->offs + offsetof(struct ehci_qtd, qtd_next),
		    sizeof(sqtd->qtd),
		    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
		stop = sqtd->qtd.qtd_next & htole32(EHCI_LINK_TERMINATE);
		usb_syncmem(&sqtd->dma,
		    sqtd->offs + offsetof(struct ehci_qtd, qtd_next),
		    sizeof(sqtd->qtd), BUS_DMASYNC_PREREAD);
	}
	if (!stop)
		printf("dump aborted, too many TDs\n");
}

void
ehci_dump_sqtd(struct ehci_soft_qtd *sqtd)
{
	usb_syncmem(&sqtd->dma, sqtd->offs, 
	    sizeof(sqtd->qtd), BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
	printf("QTD(%p) at 0x%08x:\n", sqtd, sqtd->physaddr);
	ehci_dump_qtd(&sqtd->qtd);
	usb_syncmem(&sqtd->dma, sqtd->offs, 
	    sizeof(sqtd->qtd), BUS_DMASYNC_PREREAD);
}

void
ehci_dump_qtd(struct ehci_qtd *qtd)
{
	u_int32_t s;
	char sbuf[128];

	printf("  next="); ehci_dump_link(qtd->qtd_next, 0);
	printf(" altnext="); ehci_dump_link(qtd->qtd_altnext, 0);
	printf("\n");
	s = letoh32(qtd->qtd_status);
	bitmask_snprintf(EHCI_QTD_GET_STATUS(s), "\20\10ACTIVE\7HALTED"
	    "\6BUFERR\5BABBLE\4XACTERR\3MISSED\2SPLIT\1PING",
	    sbuf, sizeof(sbuf));
	printf("  status=0x%08x: toggle=%d bytes=0x%x ioc=%d c_page=0x%x\n",
	    s, EHCI_QTD_GET_TOGGLE(s), EHCI_QTD_GET_BYTES(s),
	    EHCI_QTD_GET_IOC(s), EHCI_QTD_GET_C_PAGE(s));
	printf("    cerr=%d pid=%d stat=0x%s\n", EHCI_QTD_GET_CERR(s),
	    EHCI_QTD_GET_PID(s), sbuf);
	for (s = 0; s < 5; s++)
		printf("  buffer[%d]=0x%08x\n", s, letoh32(qtd->qtd_buffer[s]));
}

void
ehci_dump_sqh(struct ehci_soft_qh *sqh)
{
	struct ehci_qh *qh = &sqh->qh;
	u_int32_t endp, endphub;

	usb_syncmem(&sqh->dma, sqh->offs,
	    sizeof(sqh->qh), BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
	printf("QH(%p) at 0x%08x:\n", sqh, sqh->physaddr);
	printf("  link="); ehci_dump_link(qh->qh_link, 1); printf("\n");
	endp = letoh32(qh->qh_endp);
	printf("  endp=0x%08x\n", endp);
	printf("    addr=0x%02x inact=%d endpt=%d eps=%d dtc=%d hrecl=%d\n",
	    EHCI_QH_GET_ADDR(endp), EHCI_QH_GET_INACT(endp),
	    EHCI_QH_GET_ENDPT(endp),  EHCI_QH_GET_EPS(endp),
	    EHCI_QH_GET_DTC(endp), EHCI_QH_GET_HRECL(endp));
	printf("    mpl=0x%x ctl=%d nrl=%d\n",
	    EHCI_QH_GET_MPL(endp), EHCI_QH_GET_CTL(endp),
	    EHCI_QH_GET_NRL(endp));
	endphub = letoh32(qh->qh_endphub);
	printf("  endphub=0x%08x\n", endphub);
	printf("    smask=0x%02x cmask=0x%02x huba=0x%02x port=%d mult=%d\n",
	    EHCI_QH_GET_SMASK(endphub), EHCI_QH_GET_CMASK(endphub),
	    EHCI_QH_GET_HUBA(endphub), EHCI_QH_GET_PORT(endphub),
	    EHCI_QH_GET_MULT(endphub));
	printf("  curqtd="); ehci_dump_link(qh->qh_curqtd, 0); printf("\n");
	printf("Overlay qTD:\n");
	ehci_dump_qtd(&qh->qh_qtd);
	usb_syncmem(&sqh->dma, sqh->offs,
	    sizeof(sqh->qh), BUS_DMASYNC_PREREAD);
}

#if notyet
void
ehci_dump_itd(struct ehci_soft_itd *itd)
{
	ehci_isoc_trans_t t;
	ehci_isoc_bufr_ptr_t b, b2, b3;
	int i;

	printf("ITD: next phys=%X\n", itd->itd.itd_next);

	for (i = 0; i < 8; i++) {
		t = letoh32(itd->itd.itd_ctl[i]);
		printf("ITDctl %d: stat=%X len=%X ioc=%X pg=%X offs=%X\n", i,
		    EHCI_ITD_GET_STATUS(t), EHCI_ITD_GET_LEN(t),
		    EHCI_ITD_GET_IOC(t), EHCI_ITD_GET_PG(t),
		    EHCI_ITD_GET_OFFS(t));
	}
	printf("ITDbufr: ");
	for (i = 0; i < 7; i++)
		printf("%X,", EHCI_ITD_GET_BPTR(letoh32(itd->itd.itd_bufr[i])));

	b = letoh32(itd->itd.itd_bufr[0]);
	b2 = letoh32(itd->itd.itd_bufr[1]);
	b3 = letoh32(itd->itd.itd_bufr[2]);
	printf("\nep=%X daddr=%X dir=%d maxpkt=%X multi=%X\n",
	    EHCI_ITD_GET_EP(b), EHCI_ITD_GET_DADDR(b), EHCI_ITD_GET_DIR(b2),
	    EHCI_ITD_GET_MAXPKT(b2), EHCI_ITD_GET_MULTI(b3));
}
#endif

#ifdef DIAGNOSTIC
void
ehci_dump_exfer(struct ehci_xfer *ex)
{
	printf("ehci_dump_exfer: ex=%p sqtdstart=%p end=%p itdstart=%p end=%p "
	    "isdone=%d\n", ex, ex->sqtdstart, ex->sqtdend, ex->itdstart,
	    ex->itdend, ex->isdone);
}
#endif

#endif /* EHCI_DEBUG */

usbd_status
ehci_open(struct usbd_pipe *pipe)
{
	struct usbd_device *dev = pipe->device;
	struct ehci_softc *sc = (struct ehci_softc *)dev->bus;
	usb_endpoint_descriptor_t *ed = pipe->endpoint->edesc;
	u_int8_t addr = dev->address;
	u_int8_t xfertype = UE_GET_XFERTYPE(ed->bmAttributes);
	struct ehci_pipe *epipe = (struct ehci_pipe *)pipe;
	struct ehci_soft_qh *sqh;
	usbd_status err;
	int s;
	int ival, speed, naks;
	int hshubaddr, hshubport;

	DPRINTFN(1, ("ehci_open: pipe=%p, addr=%d, endpt=%d\n",
	    pipe, addr, ed->bEndpointAddress));

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	if (dev->myhsport) {
		hshubaddr = dev->myhsport->parent->address;
		hshubport = dev->myhsport->portno;
	} else {
		hshubaddr = 0;
		hshubport = 0;
	}

	/* Root Hub */
	if (pipe->device->depth == 0) {
		switch (ed->bEndpointAddress) {
		case USB_CONTROL_ENDPOINT:
			pipe->methods = &ehci_root_ctrl_methods;
			break;
		case UE_DIR_IN | EHCI_INTR_ENDPT:
			pipe->methods = &ehci_root_intr_methods;
			break;
		default:
			return (USBD_INVAL);
		}
		return (USBD_NORMAL_COMPLETION);
	}

	/* XXX All this stuff is only valid for async. */
	switch (dev->speed) {
	case USB_SPEED_LOW:
		speed = EHCI_QH_SPEED_LOW;
		break;
	case USB_SPEED_FULL:
		speed = EHCI_QH_SPEED_FULL;
		break;
	case USB_SPEED_HIGH:
		speed = EHCI_QH_SPEED_HIGH;
		break;
	default:
		panic("ehci_open: bad device speed %d", dev->speed);
	}

	/*
	 * NAK reload count:
	 * must be zero with using periodic transfer.
	 * Linux 4.20's driver (ehci-q.c) sets 4, we use same value.
	 */
	naks = ((xfertype == UE_CONTROL) || (xfertype == UE_BULK)) ? 4 : 0;

	/* Allocate sqh for everything, save isoc xfers */
	if (xfertype != UE_ISOCHRONOUS) {
		sqh = ehci_alloc_sqh(sc);
		if (sqh == NULL)
			return (USBD_NOMEM);
		/* qh_link filled when the QH is added */
		sqh->qh.qh_endp = htole32(
		    EHCI_QH_SET_ADDR(addr) |
		    EHCI_QH_SET_ENDPT(UE_GET_ADDR(ed->bEndpointAddress)) |
		    EHCI_QH_SET_EPS(speed) |
		    (xfertype == UE_CONTROL ? EHCI_QH_DTC : 0) |
		    EHCI_QH_SET_MPL(UGETW(ed->wMaxPacketSize)) |
		    (speed != EHCI_QH_SPEED_HIGH && xfertype == UE_CONTROL ?
		    EHCI_QH_CTL : 0) |
		    EHCI_QH_SET_NRL(naks)
		);
		/*
		 * To reduce conflict with split isochronous transfer,
		 * schedule (split) interrupt transfer at latter half of
		 * 1ms frame:
		 *
		 *         |<-------------- H-Frame -------------->|
		 *         .H0  :H1   H2   H3   H4   H5   H6   H7  .H0" :H1"
		 *         .    :                                  .    :
		 * [HS]    .    :          SS        CS   CS'  CS" .    :
		 * [FS/LS] .    :               |<== >>>> >>>|     .    :
		 *         .    :                                  .    :
		 *         .B7' :B0   B1   B2   B3   B4   B5   B6  .B7  :B0"
		 *              |<-------------- B-Frame -------------->|
		 *
		 */
		sqh->qh.qh_endphub = htole32(
		    EHCI_QH_SET_MULT(1) |
		    EHCI_QH_SET_SMASK(xfertype == UE_INTERRUPT ? 0x08 : 0)
		);
		if (speed != EHCI_QH_SPEED_HIGH) {
			sqh->qh.qh_endphub |= htole32(
			    EHCI_QH_SET_HUBA(hshubaddr) |
			    EHCI_QH_SET_PORT(hshubport) |
			    EHCI_QH_SET_CMASK(0xe0)
			);
		}
		sqh->qh.qh_curqtd = htole32(EHCI_LINK_TERMINATE);
		/* Fill the overlay qTD */
		sqh->qh.qh_qtd.qtd_next = htole32(EHCI_LINK_TERMINATE);
		sqh->qh.qh_qtd.qtd_altnext = htole32(EHCI_LINK_TERMINATE);
		sqh->qh.qh_qtd.qtd_status =
		    htole32(EHCI_QTD_SET_TOGGLE(pipe->endpoint->savedtoggle));

		usb_syncmem(&sqh->dma, sqh->offs, sizeof(sqh->qh),
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
		epipe->sqh = sqh;
	} /*xfertype == UE_ISOC*/

	switch (xfertype) {
	case UE_CONTROL:
		err = usb_allocmem(&sc->sc_bus, sizeof(usb_device_request_t),
		    0, USB_DMA_COHERENT, &epipe->u.ctl.reqdma);
		if (err) {
			ehci_free_sqh(sc, sqh);
			return (err);
		}
		pipe->methods = &ehci_device_ctrl_methods;
		s = splusb();
		ehci_add_qh(sqh, sc->sc_async_head);
		splx(s);
		break;
	case UE_BULK:
		pipe->methods = &ehci_device_bulk_methods;
		s = splusb();
		ehci_add_qh(sqh, sc->sc_async_head);
		splx(s);
		break;
	case UE_INTERRUPT:
		pipe->methods = &ehci_device_intr_methods;
		ival = pipe->interval;
		if (ival == USBD_DEFAULT_INTERVAL)
			ival = ed->bInterval;
		s = splusb();
		err = ehci_device_setintr(sc, sqh, ival);
		splx(s);
		return (err);
	case UE_ISOCHRONOUS:
		switch (speed) {
		case EHCI_QH_SPEED_HIGH:
		case EHCI_QH_SPEED_FULL:
			pipe->methods = &ehci_device_isoc_methods;
			break;
		case EHCI_QH_SPEED_LOW:
		default:
			return (USBD_INVAL);
		}
		/* Spec page 271 says intervals > 16 are invalid */
		if (ed->bInterval == 0 || ed->bInterval > 16) {
			printf("ehci: opening pipe with invalid bInterval\n");
			return (USBD_INVAL);
		}
		if (UGETW(ed->wMaxPacketSize) == 0) {
			printf("ehci: zero length endpoint open request\n");
			return (USBD_INVAL);
		}
		epipe->u.isoc.next_frame = 0;
		epipe->u.isoc.cur_xfers = 0;
		break;
	default:
		DPRINTF(("ehci: bad xfer type %d\n", xfertype));
		return (USBD_INVAL);
	}
	return (USBD_NORMAL_COMPLETION);
}

/*
 * Add an ED to the schedule.  Called at splusb().
 * If in the async schedule, it will always have a next.
 * If in the intr schedule it may not.
 */
void
ehci_add_qh(struct ehci_soft_qh *sqh, struct ehci_soft_qh *head)
{
	splsoftassert(IPL_SOFTUSB);

	usb_syncmem(&head->dma, head->offs + offsetof(struct ehci_qh, qh_link),
	    sizeof(head->qh.qh_link), BUS_DMASYNC_POSTWRITE);
	sqh->next = head->next;
	sqh->prev = head;
	sqh->qh.qh_link = head->qh.qh_link;
	usb_syncmem(&sqh->dma, sqh->offs + offsetof(struct ehci_qh, qh_link),
	    sizeof(sqh->qh.qh_link), BUS_DMASYNC_PREWRITE);
	head->next = sqh;
	if (sqh->next)
		sqh->next->prev = sqh;
	head->qh.qh_link = htole32(sqh->physaddr | EHCI_LINK_QH);
	usb_syncmem(&head->dma, head->offs + offsetof(struct ehci_qh, qh_link),
	    sizeof(head->qh.qh_link), BUS_DMASYNC_PREWRITE);
}

/*
 * Remove an ED from the schedule.  Called at splusb().
 * Will always have a 'next' if it's in the async list as it's circular.
 */
void
ehci_rem_qh(struct ehci_softc *sc, struct ehci_soft_qh *sqh)
{
	splsoftassert(IPL_SOFTUSB);
	/* XXX */
	usb_syncmem(&sqh->dma, sqh->offs + offsetof(struct ehci_qh, qh_link),
	    sizeof(sqh->qh.qh_link), BUS_DMASYNC_POSTWRITE);
	sqh->prev->qh.qh_link = sqh->qh.qh_link;
	sqh->prev->next = sqh->next;
	if (sqh->next)
		sqh->next->prev = sqh->prev;
	usb_syncmem(&sqh->prev->dma,
	    sqh->prev->offs + offsetof(struct ehci_qh, qh_link),
	    sizeof(sqh->prev->qh.qh_link), BUS_DMASYNC_PREWRITE);

	ehci_sync_hc(sc);
}

void
ehci_set_qh_qtd(struct ehci_soft_qh *sqh, struct ehci_soft_qtd *sqtd)
{
	int i;
	u_int32_t status;

	/* Save toggle bit and ping status. */
	usb_syncmem(&sqh->dma, sqh->offs, sizeof(sqh->qh),
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
	status = sqh->qh.qh_qtd.qtd_status &
	    htole32(EHCI_QTD_TOGGLE_MASK |
		EHCI_QTD_SET_STATUS(EHCI_QTD_PINGSTATE));
	/* Set HALTED to make hw leave it alone. */
	sqh->qh.qh_qtd.qtd_status =
	    htole32(EHCI_QTD_SET_STATUS(EHCI_QTD_HALTED));
	usb_syncmem(&sqh->dma,
	    sqh->offs + offsetof(struct ehci_qh, qh_qtd.qtd_status),
	    sizeof(sqh->qh.qh_qtd.qtd_status),
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	sqh->qh.qh_curqtd = 0;
	sqh->qh.qh_qtd.qtd_next = htole32(sqtd->physaddr);
	sqh->qh.qh_qtd.qtd_altnext = htole32(EHCI_LINK_TERMINATE);
	for (i = 0; i < EHCI_QTD_NBUFFERS; i++)
		sqh->qh.qh_qtd.qtd_buffer[i] = 0;
	sqh->sqtd = sqtd;
	usb_syncmem(&sqh->dma, sqh->offs, sizeof(sqh->qh),
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	/* Set !HALTED && !ACTIVE to start execution, preserve some fields */
	sqh->qh.qh_qtd.qtd_status = status;
	usb_syncmem(&sqh->dma,
	    sqh->offs + offsetof(struct ehci_qh, qh_qtd.qtd_status),
	    sizeof(sqh->qh.qh_qtd.qtd_status),
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
}

/*
 * Ensure that the HC has released all references to the QH.  We do this
 * by asking for a Async Advance Doorbell interrupt and then we wait for
 * the interrupt.
 * To make this easier we first obtain exclusive use of the doorbell.
 */
void
ehci_sync_hc(struct ehci_softc *sc)
{
	int s, error;
	int tries = 0;

	if (sc->sc_bus.dying) {
		return;
	}

	/* get doorbell */
	rw_enter_write(&sc->sc_doorbell_lock);
	s = splhardusb();
	do {
		EOWRITE4(sc, EHCI_USBCMD, EOREAD4(sc, EHCI_USBCMD) |
		    EHCI_CMD_IAAD);
		error = tsleep_nsec(&sc->sc_async_head, PZERO, "ehcidi",
		    MSEC_TO_NSEC(500));
	} while (error && ++tries < 10);
	splx(s);
	/* release doorbell */
	rw_exit_write(&sc->sc_doorbell_lock);
#ifdef DIAGNOSTIC
	if (error)
		printf("ehci_sync_hc: tsleep() = %d\n", error);
#endif
}

void
ehci_rem_itd_chain(struct ehci_softc *sc, struct ehci_xfer *ex)
{
	struct ehci_soft_itd *itd, *prev = NULL;

	splsoftassert(IPL_SOFTUSB);

	KASSERT(ex->itdstart != NULL && ex->itdend != NULL);

	for (itd = ex->itdstart; itd != NULL; itd = itd->xfer_next) {
		prev = itd->u.frame_list.prev;
		/* Unlink itd from hardware chain, or frame array */
		if (prev == NULL) { /* We're at the table head */
			sc->sc_softitds[itd->slot] = itd->u.frame_list.next;
			sc->sc_flist[itd->slot] = itd->itd.itd_next;
			usb_syncmem(&sc->sc_fldma,
			    sizeof(uint32_t) * itd->slot, sizeof(uint32_t),
			    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

			if (itd->u.frame_list.next != NULL)
				itd->u.frame_list.next->u.frame_list.prev =
				    NULL;
		} else {
			/* XXX this part is untested... */
			prev->itd.itd_next = itd->itd.itd_next;
			usb_syncmem(&itd->dma,
			    itd->offs + offsetof(struct ehci_itd, itd_next),
			    sizeof(itd->itd.itd_next), BUS_DMASYNC_PREWRITE);

			prev->u.frame_list.next = itd->u.frame_list.next;
			if (itd->u.frame_list.next != NULL)
				itd->u.frame_list.next->u.frame_list.prev =
				    prev;
		}
	}
}

void
ehci_free_itd_chain(struct ehci_softc *sc, struct ehci_xfer *ex)
{
	struct ehci_soft_itd *itd, *prev = NULL;

	splsoftassert(IPL_SOFTUSB);

	KASSERT(ex->itdstart != NULL && ex->itdend != NULL);

	for (itd = ex->itdstart; itd != NULL; itd = itd->xfer_next) {
		if (prev != NULL)
			ehci_free_itd(sc, prev);
		prev = itd;
	}
	if (prev)
		ehci_free_itd(sc, prev);
	ex->itdstart = NULL;
	ex->itdend = NULL;
}

/*
 * Data structures and routines to emulate the root hub.
 */
const usb_device_descriptor_t ehci_devd = {
	USB_DEVICE_DESCRIPTOR_SIZE,
	UDESC_DEVICE,		/* type */
	{0x00, 0x02},		/* USB version */
	UDCLASS_HUB,		/* class */
	UDSUBCLASS_HUB,		/* subclass */
	UDPROTO_HSHUBSTT,	/* protocol */
	64,			/* max packet */
	{0},{0},{0x00,0x01},	/* device id */
	1,2,0,			/* string indices */
	1			/* # of configurations */
};

const usb_device_qualifier_t ehci_odevd = {
	USB_DEVICE_DESCRIPTOR_SIZE,
	UDESC_DEVICE_QUALIFIER,	/* type */
	{0x00, 0x02},		/* USB version */
	UDCLASS_HUB,		/* class */
	UDSUBCLASS_HUB,		/* subclass */
	UDPROTO_FSHUB,		/* protocol */
	64,			/* max packet */
	1,			/* # of configurations */
	0
};

const usb_config_descriptor_t ehci_confd = {
	USB_CONFIG_DESCRIPTOR_SIZE,
	UDESC_CONFIG,
	{USB_CONFIG_DESCRIPTOR_SIZE +
	 USB_INTERFACE_DESCRIPTOR_SIZE +
	 USB_ENDPOINT_DESCRIPTOR_SIZE},
	1,
	1,
	0,
	UC_BUS_POWERED | UC_SELF_POWERED,
	0			/* max power */
};

const usb_interface_descriptor_t ehci_ifcd = {
	USB_INTERFACE_DESCRIPTOR_SIZE,
	UDESC_INTERFACE,
	0,
	0,
	1,
	UICLASS_HUB,
	UISUBCLASS_HUB,
	UIPROTO_HSHUBSTT,
	0
};

const usb_endpoint_descriptor_t ehci_endpd = {
	USB_ENDPOINT_DESCRIPTOR_SIZE,
	UDESC_ENDPOINT,
	UE_DIR_IN | EHCI_INTR_ENDPT,
	UE_INTERRUPT,
	{8, 0},			/* max packet */
	12
};

const usb_hub_descriptor_t ehci_hubd = {
	USB_HUB_DESCRIPTOR_SIZE,
	UDESC_HUB,
	0,
	{0,0},
	0,
	0,
	{0},
};

/*
 * Simulate a hardware hub by handling all the necessary requests.
 */
usbd_status
ehci_root_ctrl_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	/* Insert last in queue. */
	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	/* Pipe isn't running, start first */
	return (ehci_root_ctrl_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
ehci_root_ctrl_start(struct usbd_xfer *xfer)
{
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;
	usb_device_request_t *req;
	void *buf = NULL;
	int port, i;
	int s, len, value, index, l, totlen = 0;
	usb_port_status_t ps;
	usb_device_descriptor_t devd;
	usb_hub_descriptor_t hubd;
	usbd_status err;
	u_int32_t v;

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

#ifdef DIAGNOSTIC
	if (!(xfer->rqflags & URQ_REQUEST))
		/* XXX panic */
		return (USBD_INVAL);
#endif
	req = &xfer->request;

	DPRINTFN(4,("ehci_root_ctrl_start: type=0x%02x request=%02x\n",
		    req->bmRequestType, req->bRequest));

	len = UGETW(req->wLength);
	value = UGETW(req->wValue);
	index = UGETW(req->wIndex);

	if (len != 0)
		buf = KERNADDR(&xfer->dmabuf, 0);

#define C(x,y) ((x) | ((y) << 8))
	switch(C(req->bRequest, req->bmRequestType)) {
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
			*(u_int8_t *)buf = sc->sc_conf;
			totlen = 1;
		}
		break;
	case C(UR_GET_DESCRIPTOR, UT_READ_DEVICE):
		DPRINTFN(8,("ehci_root_ctrl_start: wValue=0x%04x\n", value));
		switch(value >> 8) {
		case UDESC_DEVICE:
			if ((value & 0xff) != 0) {
				err = USBD_IOERROR;
				goto ret;
			}
			devd = ehci_devd;
			USETW(devd.idVendor, sc->sc_id_vendor);
			totlen = l = min(len, USB_DEVICE_DESCRIPTOR_SIZE);
			memcpy(buf, &devd, l);
			break;
		case UDESC_DEVICE_QUALIFIER:
			if ((value & 0xff) != 0) {
				err = USBD_IOERROR;
				goto ret;
			}
			totlen = l = min(len, USB_DEVICE_DESCRIPTOR_SIZE);
			memcpy(buf, &ehci_odevd, l);
			break;
		/*
		 * We can't really operate at another speed, but the spec says
		 * we need this descriptor.
		 */
		case UDESC_OTHER_SPEED_CONFIGURATION:
		case UDESC_CONFIG:
			if ((value & 0xff) != 0) {
				err = USBD_IOERROR;
				goto ret;
			}
			totlen = l = min(len, USB_CONFIG_DESCRIPTOR_SIZE);
			memcpy(buf, &ehci_confd, l);
			((usb_config_descriptor_t *)buf)->bDescriptorType =
			    value >> 8;
			buf = (char *)buf + l;
			len -= l;
			l = min(len, USB_INTERFACE_DESCRIPTOR_SIZE);
			totlen += l;
			memcpy(buf, &ehci_ifcd, l);
			buf = (char *)buf + l;
			len -= l;
			l = min(len, USB_ENDPOINT_DESCRIPTOR_SIZE);
			totlen += l;
			memcpy(buf, &ehci_endpd, l);
			break;
		case UDESC_STRING:
			if (len == 0)
				break;
			*(u_int8_t *)buf = 0;
			totlen = 1;
			switch (value & 0xff) {
			case 0: /* Language table */
				totlen = usbd_str(buf, len, "\001");
				break;
			case 1: /* Vendor */
				totlen = usbd_str(buf, len, sc->sc_vendor);
				break;
			case 2: /* Product */
				totlen = usbd_str(buf, len, "EHCI root hub");
				break;
			}
			break;
		default:
			err = USBD_IOERROR;
			goto ret;
		}
		break;
	case C(UR_GET_INTERFACE, UT_READ_INTERFACE):
		if (len > 0) {
			*(u_int8_t *)buf = 0;
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
		if (value >= USB_MAX_DEVICES) {
			err = USBD_IOERROR;
			goto ret;
		}
		break;
	case C(UR_SET_CONFIG, UT_WRITE_DEVICE):
		if (value != 0 && value != 1) {
			err = USBD_IOERROR;
			goto ret;
		}
		sc->sc_conf = value;
		break;
	case C(UR_SET_DESCRIPTOR, UT_WRITE_DEVICE):
		break;
	case C(UR_SET_FEATURE, UT_WRITE_DEVICE):
	case C(UR_SET_FEATURE, UT_WRITE_INTERFACE):
	case C(UR_SET_FEATURE, UT_WRITE_ENDPOINT):
		err = USBD_IOERROR;
		goto ret;
	case C(UR_SET_INTERFACE, UT_WRITE_INTERFACE):
		break;
	case C(UR_SYNCH_FRAME, UT_WRITE_ENDPOINT):
		break;
	/* Hub requests */
	case C(UR_CLEAR_FEATURE, UT_WRITE_CLASS_DEVICE):
		break;
	case C(UR_CLEAR_FEATURE, UT_WRITE_CLASS_OTHER):
		DPRINTFN(8, ("ehci_root_ctrl_start: UR_CLEAR_PORT_FEATURE "
		    "port=%d feature=%d\n", index, value));
		if (index < 1 || index > sc->sc_noport) {
			err = USBD_IOERROR;
			goto ret;
		}
		port = EHCI_PORTSC(index);
		v = EOREAD4(sc, port) &~ EHCI_PS_CLEAR;
		switch(value) {
		case UHF_PORT_ENABLE:
			EOWRITE4(sc, port, v &~ EHCI_PS_PE);
			break;
		case UHF_PORT_SUSPEND:
			EOWRITE4(sc, port, v &~ EHCI_PS_SUSP);
			break;
		case UHF_PORT_POWER:
			EOWRITE4(sc, port, v &~ EHCI_PS_PP);
			break;
		case UHF_PORT_TEST:
			DPRINTFN(2,("ehci_root_ctrl_start: "
			    "clear port test %d\n", index));
			break;
		case UHF_PORT_INDICATOR:
			DPRINTFN(2,("ehci_root_ctrl_start: "
			    "clear port index %d\n", index));
			EOWRITE4(sc, port, v &~ EHCI_PS_PIC);
			break;
		case UHF_C_PORT_CONNECTION:
			EOWRITE4(sc, port, v | EHCI_PS_CSC);
			break;
		case UHF_C_PORT_ENABLE:
			EOWRITE4(sc, port, v | EHCI_PS_PEC);
			break;
		case UHF_C_PORT_SUSPEND:
			/* how? */
			break;
		case UHF_C_PORT_OVER_CURRENT:
			EOWRITE4(sc, port, v | EHCI_PS_OCC);
			break;
		case UHF_C_PORT_RESET:
			sc->sc_isreset = 0;
			break;
		default:
			err = USBD_IOERROR;
			goto ret;
		}
		break;
	case C(UR_GET_DESCRIPTOR, UT_READ_CLASS_DEVICE):
		if ((value & 0xff) != 0) {
			err = USBD_IOERROR;
			goto ret;
		}
		hubd = ehci_hubd;
		hubd.bNbrPorts = sc->sc_noport;
		v = EREAD4(sc, EHCI_HCSPARAMS);
		USETW(hubd.wHubCharacteristics,
		    (EHCI_HCS_PPC(v) ? UHD_PWR_INDIVIDUAL : UHD_PWR_NO_SWITCH) |
		    (EHCI_HCS_P_INDICATOR(v) ? UHD_PORT_IND : 0));
		hubd.bPwrOn2PwrGood = 200; /* XXX can't find out? */
		for (i = 0, l = sc->sc_noport; l > 0; i++, l -= 8, v >>= 8)
			hubd.DeviceRemovable[i++] = 0; /* XXX can't find out? */
		hubd.bDescLength = USB_HUB_DESCRIPTOR_SIZE + i;
		l = min(len, hubd.bDescLength);
		totlen = l;
		memcpy(buf, &hubd, l);
		break;
	case C(UR_GET_STATUS, UT_READ_CLASS_DEVICE):
		if (len != 4) {
			err = USBD_IOERROR;
			goto ret;
		}
		memset(buf, 0, len); /* ? XXX */
		totlen = len;
		break;
	case C(UR_GET_STATUS, UT_READ_CLASS_OTHER):
		DPRINTFN(8,("ehci_root_ctrl_start: get port status i=%d\n",
		    index));
		if (index < 1 || index > sc->sc_noport) {
			err = USBD_IOERROR;
			goto ret;
		}
		if (len != 4) {
			err = USBD_IOERROR;
			goto ret;
		}
		v = EOREAD4(sc, EHCI_PORTSC(index));
		DPRINTFN(8,("ehci_root_ctrl_start: port status=0x%04x\n", v));
		i = UPS_HIGH_SPEED;
		if (v & EHCI_PS_CS)	i |= UPS_CURRENT_CONNECT_STATUS;
		if (v & EHCI_PS_PE)	i |= UPS_PORT_ENABLED;
		if (v & EHCI_PS_SUSP)	i |= UPS_SUSPEND;
		if (v & EHCI_PS_OCA)	i |= UPS_OVERCURRENT_INDICATOR;
		if (v & EHCI_PS_PR)	i |= UPS_RESET;
		if (v & EHCI_PS_PP)	i |= UPS_PORT_POWER;
		USETW(ps.wPortStatus, i);
		i = 0;
		if (v & EHCI_PS_CSC)	i |= UPS_C_CONNECT_STATUS;
		if (v & EHCI_PS_PEC)	i |= UPS_C_PORT_ENABLED;
		if (v & EHCI_PS_OCC)	i |= UPS_C_OVERCURRENT_INDICATOR;
		if (sc->sc_isreset)	i |= UPS_C_PORT_RESET;
		USETW(ps.wPortChange, i);
		l = min(len, sizeof(ps));
		memcpy(buf, &ps, l);
		totlen = l;
		break;
	case C(UR_SET_DESCRIPTOR, UT_WRITE_CLASS_DEVICE):
		err = USBD_IOERROR;
		goto ret;
	case C(UR_SET_FEATURE, UT_WRITE_CLASS_DEVICE):
		break;
	case C(UR_SET_FEATURE, UT_WRITE_CLASS_OTHER):
		if (index < 1 || index > sc->sc_noport) {
			err = USBD_IOERROR;
			goto ret;
		}
		port = EHCI_PORTSC(index);
		v = EOREAD4(sc, port) &~ EHCI_PS_CLEAR;
		switch(value) {
		case UHF_PORT_ENABLE:
			EOWRITE4(sc, port, v | EHCI_PS_PE);
			break;
		case UHF_PORT_SUSPEND:
			EOWRITE4(sc, port, v | EHCI_PS_SUSP);
			break;
		case UHF_PORT_DISOWN_TO_1_1:
			/* enter to Port Reset State */
			v &= ~EHCI_PS_PE;
			EOWRITE4(sc, port, v | EHCI_PS_PR);
			ehci_disown(sc, index, 0);
			break;
		case UHF_PORT_RESET:
			DPRINTFN(5,("ehci_root_ctrl_start: reset port %d\n",
			    index));
			if (EHCI_PS_IS_LOWSPEED(v)) {
				/* Low speed device, give up ownership. */
				ehci_disown(sc, index, 1);
				break;
			}
			/* Start reset sequence. */
			v &= ~ (EHCI_PS_PE | EHCI_PS_PR);
			EOWRITE4(sc, port, v | EHCI_PS_PR);
			/* Wait for reset to complete. */
			usb_delay_ms(&sc->sc_bus, USB_PORT_ROOT_RESET_DELAY);
			if (sc->sc_bus.dying) {
				err = USBD_IOERROR;
				goto ret;
			}
			/* Terminate reset sequence. */
			v = EOREAD4(sc, port);
			EOWRITE4(sc, port, v & ~EHCI_PS_PR);
			/* Wait for HC to complete reset. */
			usb_delay_ms(&sc->sc_bus, EHCI_PORT_RESET_COMPLETE);
			if (sc->sc_bus.dying) {
				err = USBD_IOERROR;
				goto ret;
			}
			v = EOREAD4(sc, port);
			DPRINTF(("ehci after reset, status=0x%08x\n", v));
			if (v & EHCI_PS_PR) {
				printf("%s: port reset timeout\n",
				    sc->sc_bus.bdev.dv_xname);
				err = USBD_IOERROR;
				goto ret;
			}
			if (!(v & EHCI_PS_PE)) {
				/* Not a high speed device, give up ownership.*/
				ehci_disown(sc, index, 0);
				break;
			}
			sc->sc_isreset = 1;
			DPRINTF(("ehci port %d reset, status = 0x%08x\n",
			    index, v));
			break;
		case UHF_PORT_POWER:
			DPRINTFN(2,("ehci_root_ctrl_start: "
			    "set port power %d\n", index));
			EOWRITE4(sc, port, v | EHCI_PS_PP);
			break;
		case UHF_PORT_TEST:
			DPRINTFN(2,("ehci_root_ctrl_start: "
			    "set port test %d\n", index));
			break;
		case UHF_PORT_INDICATOR:
			DPRINTFN(2,("ehci_root_ctrl_start: "
			    "set port ind %d\n", index));
			EOWRITE4(sc, port, v | EHCI_PS_PIC);
			break;
		default:
			err = USBD_IOERROR;
			goto ret;
		}
		break;
	case C(UR_CLEAR_TT_BUFFER, UT_WRITE_CLASS_OTHER):
	case C(UR_RESET_TT, UT_WRITE_CLASS_OTHER):
	case C(UR_GET_TT_STATE, UT_READ_CLASS_OTHER):
	case C(UR_STOP_TT, UT_WRITE_CLASS_OTHER):
		break;
	default:
		err = USBD_IOERROR;
		goto ret;
	}
	xfer->actlen = totlen;
	err = USBD_NORMAL_COMPLETION;
 ret:
	xfer->status = err;
	s = splusb();
	usb_transfer_complete(xfer);
	splx(s);
	return (err);
}

void
ehci_disown(struct ehci_softc *sc, int index, int lowspeed)
{
	int port;
	u_int32_t v;

	port = EHCI_PORTSC(index);
	v = EOREAD4(sc, port) &~ EHCI_PS_CLEAR;
	EOWRITE4(sc, port, v | EHCI_PS_PO);
}

/* Abort a root control request. */
void
ehci_root_ctrl_abort(struct usbd_xfer *xfer)
{
	/* Nothing to do, all transfers are synchronous. */
}

/* Close the root pipe. */
void
ehci_root_ctrl_close(struct usbd_pipe *pipe)
{
	/* Nothing to do. */
}

void
ehci_root_intr_done(struct usbd_xfer *xfer)
{
}

usbd_status
ehci_root_intr_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	/* Insert last in queue. */
	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	/* Pipe isn't running, start first */
	return (ehci_root_intr_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
ehci_root_intr_start(struct usbd_xfer *xfer)
{
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	sc->sc_intrxfer = xfer;

	return (USBD_IN_PROGRESS);
}

void
ehci_root_intr_abort(struct usbd_xfer *xfer)
{
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;
	int s;

	sc->sc_intrxfer = NULL;

	xfer->status = USBD_CANCELLED;
	s = splusb();
	usb_transfer_complete(xfer);
	splx(s);
}

void
ehci_root_intr_close(struct usbd_pipe *pipe)
{
}

void
ehci_root_ctrl_done(struct usbd_xfer *xfer)
{
}

struct ehci_soft_qh *
ehci_alloc_sqh(struct ehci_softc *sc)
{
	struct ehci_soft_qh *sqh = NULL;
	usbd_status err;
	int i, offs;
	struct usb_dma dma;
	int s;

	s = splusb();
	if (sc->sc_freeqhs == NULL) {
		DPRINTFN(2, ("ehci_alloc_sqh: allocating chunk\n"));
		err = usb_allocmem(&sc->sc_bus, EHCI_SQH_SIZE * EHCI_SQH_CHUNK,
		    EHCI_PAGE_SIZE, USB_DMA_COHERENT, &dma);
		if (err)
			goto out;
		for (i = 0; i < EHCI_SQH_CHUNK; i++) {
			offs = i * EHCI_SQH_SIZE;
			sqh = KERNADDR(&dma, offs);
			sqh->physaddr = DMAADDR(&dma, offs);
			sqh->dma = dma;
			sqh->offs = offs;
			sqh->next = sc->sc_freeqhs;
			sc->sc_freeqhs = sqh;
		}
	}
	sqh = sc->sc_freeqhs;
	sc->sc_freeqhs = sqh->next;
	memset(&sqh->qh, 0, sizeof(struct ehci_qh));
	sqh->next = NULL;
	sqh->prev = NULL;

out:
	splx(s);
	return (sqh);
}

void
ehci_free_sqh(struct ehci_softc *sc, struct ehci_soft_qh *sqh)
{
	int s;

	s = splusb();
	sqh->next = sc->sc_freeqhs;
	sc->sc_freeqhs = sqh;
	splx(s);
}

struct ehci_soft_qtd *
ehci_alloc_sqtd(struct ehci_softc *sc)
{
	struct ehci_soft_qtd *sqtd = NULL;
	usbd_status err;
	int i, offs;
	struct usb_dma dma;
	int s;

	s = splusb();
	if (sc->sc_freeqtds == NULL) {
		DPRINTFN(2, ("ehci_alloc_sqtd: allocating chunk\n"));
		err = usb_allocmem(&sc->sc_bus, EHCI_SQTD_SIZE*EHCI_SQTD_CHUNK,
		    EHCI_PAGE_SIZE, USB_DMA_COHERENT, &dma);
		if (err)
			goto out;
		for(i = 0; i < EHCI_SQTD_CHUNK; i++) {
			offs = i * EHCI_SQTD_SIZE;
			sqtd = KERNADDR(&dma, offs);
			sqtd->physaddr = DMAADDR(&dma, offs);
			sqtd->dma = dma;
			sqtd->offs = offs;
			sqtd->nextqtd = sc->sc_freeqtds;
			sc->sc_freeqtds = sqtd;
		}
	}

	sqtd = sc->sc_freeqtds;
	sc->sc_freeqtds = sqtd->nextqtd;
	memset(&sqtd->qtd, 0, sizeof(struct ehci_qtd));
	sqtd->nextqtd = NULL;

out:
	splx(s);
	return (sqtd);
}

void
ehci_free_sqtd(struct ehci_softc *sc, struct ehci_soft_qtd *sqtd)
{
	int s;

	s = splusb();
	sqtd->nextqtd = sc->sc_freeqtds;
	sc->sc_freeqtds = sqtd;
	splx(s);
}

usbd_status
ehci_alloc_sqtd_chain(struct ehci_softc *sc, u_int alen, struct usbd_xfer *xfer,
    struct ehci_soft_qtd **sp, struct ehci_soft_qtd **ep)
{
	struct ehci_soft_qtd *next, *cur;
	ehci_physaddr_t dataphys, dataphyspage, dataphyslastpage, nextphys;
	u_int32_t qtdstatus;
	u_int len, curlen;
	int mps, i, iscontrol, forceshort;
	int rd = usbd_xfer_isread(xfer);
	struct usb_dma *dma = &xfer->dmabuf;

	DPRINTFN(alen<4*4096,("ehci_alloc_sqtd_chain: start len=%d\n", alen));

	len = alen;
	iscontrol = UE_GET_XFERTYPE(xfer->pipe->endpoint->edesc->bmAttributes) ==
	    UE_CONTROL;

	dataphys = DMAADDR(dma, 0);
	dataphyslastpage = EHCI_PAGE(dataphys + len - 1);
	qtdstatus = EHCI_QTD_ACTIVE |
	    EHCI_QTD_SET_PID(rd ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT) |
	    EHCI_QTD_SET_CERR(3); /* IOC and BYTES set below */
	mps = UGETW(xfer->pipe->endpoint->edesc->wMaxPacketSize);
	forceshort = ((xfer->flags & USBD_FORCE_SHORT_XFER) || len == 0) &&
	    len % mps == 0;
	/*
	 * The control transfer data stage always starts with a toggle of 1.
	 * For other transfers we let the hardware track the toggle state.
	 */
	if (iscontrol)
		qtdstatus |= EHCI_QTD_SET_TOGGLE(1);

	cur = ehci_alloc_sqtd(sc);
	*sp = cur;
	if (cur == NULL)
		goto nomem;

	usb_syncmem(dma, 0, alen,
	    rd ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);
	for (;;) {
		dataphyspage = EHCI_PAGE(dataphys);
		/* The EHCI hardware can handle at most 5 pages. */
		if (dataphyslastpage - dataphyspage <
		    EHCI_QTD_NBUFFERS * EHCI_PAGE_SIZE) {
			/* we can handle it in this QTD */
			curlen = len;
		} else {
			/* must use multiple TDs, fill as much as possible. */
			curlen = EHCI_QTD_NBUFFERS * EHCI_PAGE_SIZE -
				 EHCI_PAGE_OFFSET(dataphys);

			if (curlen > len) {
				DPRINTFN(1,("ehci_alloc_sqtd_chain: curlen=%u "
				    "len=%u offs=0x%x\n", curlen, len,
				    EHCI_PAGE_OFFSET(dataphys)));
				DPRINTFN(1,("lastpage=0x%x page=0x%x phys=0x%x\n",
				    dataphyslastpage, dataphyspage, dataphys));
				curlen = len;
			}

			/* the length must be a multiple of the max size */
			curlen -= curlen % mps;
			DPRINTFN(1,("ehci_alloc_sqtd_chain: multiple QTDs, "
			    "curlen=%u\n", curlen));
		}

		DPRINTFN(4,("ehci_alloc_sqtd_chain: dataphys=0x%08x "
		    "dataphyslastpage=0x%08x len=%u curlen=%u\n",
		    dataphys, dataphyslastpage, len, curlen));
		len -= curlen;

		/*
		 * Allocate another transfer if there's more data left,
		 * or if force last short transfer flag is set and we're
		 * allocating a multiple of the max packet size.
		 */
		if (len != 0 || forceshort) {
			next = ehci_alloc_sqtd(sc);
			if (next == NULL)
				goto nomem;
			nextphys = htole32(next->physaddr);
		} else {
			next = NULL;
			nextphys = htole32(EHCI_LINK_TERMINATE);
		}

		for (i = 0; i * EHCI_PAGE_SIZE <
		    curlen + EHCI_PAGE_OFFSET(dataphys); i++) {
			ehci_physaddr_t a = dataphys + i * EHCI_PAGE_SIZE;
			if (i != 0) /* use offset only in first buffer */
				a = EHCI_PAGE(a);
#ifdef DIAGNOSTIC
			if (i >= EHCI_QTD_NBUFFERS) {
				printf("ehci_alloc_sqtd_chain: i=%d\n", i);
				goto nomem;
			}
#endif
			cur->qtd.qtd_buffer[i] = htole32(a);
			cur->qtd.qtd_buffer_hi[i] = 0;
		}
		cur->nextqtd = next;
		cur->qtd.qtd_next = cur->qtd.qtd_altnext = nextphys;
		cur->qtd.qtd_status = htole32(qtdstatus |
		    EHCI_QTD_SET_BYTES(curlen));
		cur->len = curlen;
		DPRINTFN(10,("ehci_alloc_sqtd_chain: cbp=0x%08x end=0x%08x\n",
		    dataphys, dataphys + curlen));
		DPRINTFN(10,("ehci_alloc_sqtd_chain: curlen=%u\n", curlen));
		if (iscontrol) {
			/*
			 * adjust the toggle based on the number of packets
			 * in this qtd
			 */
			if ((((curlen + mps - 1) / mps) & 1) || curlen == 0)
				qtdstatus ^= EHCI_QTD_TOGGLE_MASK;
		}
		if (len == 0) {
			if (! forceshort)
				break;
			forceshort = 0;
		}
		usb_syncmem(&cur->dma, cur->offs, sizeof(cur->qtd),
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
		DPRINTFN(10,("ehci_alloc_sqtd_chain: extend chain\n"));
		dataphys += curlen;
		cur = next;
	}
	cur->qtd.qtd_status |= htole32(EHCI_QTD_IOC);
	usb_syncmem(&cur->dma, cur->offs, sizeof(cur->qtd),
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	*ep = cur;

	DPRINTFN(10,("ehci_alloc_sqtd_chain: return sqtd=%p sqtdend=%p\n",
	    *sp, *ep));

	return (USBD_NORMAL_COMPLETION);

 nomem:
	/* XXX free chain */
	DPRINTFN(-1,("ehci_alloc_sqtd_chain: no memory\n"));
	return (USBD_NOMEM);
}

void
ehci_free_sqtd_chain(struct ehci_softc *sc, struct ehci_xfer *ex)
{
	struct ehci_pipe *epipe = (struct ehci_pipe *)ex->xfer.pipe;
	struct ehci_soft_qtd *sqtd, *next;

	DPRINTFN(10,("ehci_free_sqtd_chain: sqtd=%p\n", ex->sqtdstart));

	for (sqtd = ex->sqtdstart; sqtd != NULL; sqtd = next) {
		next = sqtd->nextqtd;
		ehci_free_sqtd(sc, sqtd);
	}
	ex->sqtdstart = ex->sqtdend = NULL;
	epipe->sqh->sqtd = NULL;
}

struct ehci_soft_itd *
ehci_alloc_itd(struct ehci_softc *sc)
{
	struct ehci_soft_itd *itd, *freeitd;
	usbd_status err;
	int i, s, offs, frindex, previndex;
	struct usb_dma dma;

	s = splusb();

	/* Find an itd that wasn't freed this frame or last frame. This can
	 * discard itds that were freed before frindex wrapped around
	 * XXX - can this lead to thrashing? Could fix by enabling wrap-around
	 *       interrupt and fiddling with list when that happens */
	frindex = (EOREAD4(sc, EHCI_FRINDEX) + 1) >> 3;
	previndex = (frindex != 0) ? frindex - 1 : sc->sc_flsize;

	freeitd = NULL;
	LIST_FOREACH(itd, &sc->sc_freeitds, u.free_list) {
		if (itd->slot != frindex && itd->slot != previndex) {
			freeitd = itd;
			break;
		}
	}

	if (freeitd == NULL) {
		err = usb_allocmem(&sc->sc_bus, EHCI_ITD_SIZE * EHCI_ITD_CHUNK,
		    EHCI_PAGE_SIZE, USB_DMA_COHERENT, &dma);
		if (err) {
			splx(s);
			return (NULL);
		}

		for (i = 0; i < EHCI_ITD_CHUNK; i++) {
			offs = i * EHCI_ITD_SIZE;
			itd = KERNADDR(&dma, offs);
			itd->physaddr = DMAADDR(&dma, offs);
			itd->dma = dma;
			itd->offs = offs;
			LIST_INSERT_HEAD(&sc->sc_freeitds, itd, u.free_list);
		}
		freeitd = LIST_FIRST(&sc->sc_freeitds);
	}

	itd = freeitd;
	LIST_REMOVE(itd, u.free_list);
	memset(&itd->itd, 0, sizeof(struct ehci_itd));
	usb_syncmem(&itd->dma, itd->offs + offsetof(struct ehci_itd, itd_next),
	    sizeof(itd->itd.itd_next), BUS_DMASYNC_PREWRITE |
	    BUS_DMASYNC_PREREAD);

	itd->u.frame_list.next = NULL;
	itd->u.frame_list.prev = NULL;
	itd->xfer_next = NULL;
	itd->slot = 0;
	splx(s);

	return (itd);
}

void
ehci_free_itd(struct ehci_softc *sc, struct ehci_soft_itd *itd)
{
	int s;

	s = splusb();
	LIST_INSERT_HEAD(&sc->sc_freeitds, itd, u.free_list);
	splx(s);
}

/*
 * Close a regular pipe.
 * Assumes that there are no pending transactions.
 */
void
ehci_close_pipe(struct usbd_pipe *pipe)
{
	struct ehci_pipe *epipe = (struct ehci_pipe *)pipe;
	struct ehci_softc *sc = (struct ehci_softc *)pipe->device->bus;
	struct ehci_soft_qh *sqh = epipe->sqh;
	int s;

	s = splusb();
	ehci_rem_qh(sc, sqh);
	splx(s);
	pipe->endpoint->savedtoggle =
	    EHCI_QTD_GET_TOGGLE(letoh32(sqh->qh.qh_qtd.qtd_status));
	ehci_free_sqh(sc, epipe->sqh);
}

/*
 * Abort a device request.
 * If this routine is called at splusb() it guarantees that the request
 * will be removed from the hardware scheduling and that the callback
 * for it will be called with USBD_CANCELLED status.
 * It's impossible to guarantee that the requested transfer will not
 * have happened since the hardware runs concurrently.
 * If the transaction has already happened we rely on the ordinary
 * interrupt processing to process it.
 */
void
ehci_abort_xfer(struct usbd_xfer *xfer, usbd_status status)
{
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;
	struct ehci_pipe *epipe = (struct ehci_pipe *)xfer->pipe;
	struct ehci_xfer *ex = (struct ehci_xfer*)xfer;
	struct ehci_soft_qh *sqh = epipe->sqh;
	struct ehci_soft_qtd *sqtd;
	int s;

	if (sc->sc_bus.dying || xfer->status == USBD_NOT_STARTED) {
		s = splusb();
		if (xfer->status != USBD_NOT_STARTED)
			TAILQ_REMOVE(&sc->sc_intrhead, ex, inext);
		xfer->status = status;	/* make software ignore it */
		timeout_del(&xfer->timeout_handle);
		usb_rem_task(xfer->device, &xfer->abort_task);
#ifdef DIAGNOSTIC
		ex->isdone = 1;
#endif
		usb_transfer_complete(xfer);
		splx(s);
		return;
	}

	if (xfer->device->bus->intr_context)
		panic("ehci_abort_xfer: not in process context");

	/*
	 * If an abort is already in progress then just wait for it to
	 * complete and return.
	 */
	if (ex->ehci_xfer_flags & EHCI_XFER_ABORTING) {
		DPRINTFN(2, ("ehci_abort_xfer: already aborting\n"));
		/* No need to wait if we're aborting from a timeout. */
		if (status == USBD_TIMEOUT)
			return;
		/* Override the status which might be USBD_TIMEOUT. */
		xfer->status = status;
		DPRINTFN(2, ("ehci_abort_xfer: waiting for abort to finish\n"));
		ex->ehci_xfer_flags |= EHCI_XFER_ABORTWAIT;
		while (ex->ehci_xfer_flags & EHCI_XFER_ABORTING)
			tsleep_nsec(&ex->ehci_xfer_flags, PZERO, "ehciaw", INFSLP);
		return;
	}

	/*
	 * Step 1: Make interrupt routine and timeouts ignore xfer.
	 */
	s = splusb();
	ex->ehci_xfer_flags |= EHCI_XFER_ABORTING;
	xfer->status = status;	/* make software ignore it */
	TAILQ_REMOVE(&sc->sc_intrhead, ex, inext);
	timeout_del(&xfer->timeout_handle);
	usb_rem_task(xfer->device, &xfer->abort_task);
	splx(s);

	/*
	 * Step 2: Deactivate all of the qTDs that we will be removing,
	 * otherwise the queue head may go active again.
	 */
	usb_syncmem(&sqh->dma,
	    sqh->offs + offsetof(struct ehci_qh, qh_qtd.qtd_status),
	    sizeof(sqh->qh.qh_qtd.qtd_status),
	    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
	sqh->qh.qh_qtd.qtd_status = htole32(EHCI_QTD_HALTED);
	usb_syncmem(&sqh->dma,
	    sqh->offs + offsetof(struct ehci_qh, qh_qtd.qtd_status),
	    sizeof(sqh->qh.qh_qtd.qtd_status),
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	for (sqtd = ex->sqtdstart; sqtd != NULL; sqtd = sqtd->nextqtd) {
		usb_syncmem(&sqtd->dma,
		    sqtd->offs + offsetof(struct ehci_qtd, qtd_status),
		    sizeof(sqtd->qtd.qtd_status),
		    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
		sqtd->qtd.qtd_status = htole32(EHCI_QTD_HALTED);
		usb_syncmem(&sqtd->dma,
		    sqtd->offs + offsetof(struct ehci_qtd, qtd_status),
		    sizeof(sqtd->qtd.qtd_status),
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	}
	ehci_sync_hc(sc);

	/*
	 * Step 3: Make sure the soft interrupt routine has run. This
	 * should remove any completed items off the queue.
	 * The hardware has no reference to completed items (TDs).
	 * It's safe to remove them at any time.
	 */
	s = splusb();
	sc->sc_softwake = 1;
	usb_schedsoftintr(&sc->sc_bus);
	tsleep_nsec(&sc->sc_softwake, PZERO, "ehciab", INFSLP);

#ifdef DIAGNOSTIC
	ex->isdone = 1;
#endif
	/* Do the wakeup first to avoid touching the xfer after the callback. */
	ex->ehci_xfer_flags &= ~EHCI_XFER_ABORTING;
	if (ex->ehci_xfer_flags & EHCI_XFER_ABORTWAIT) {
		ex->ehci_xfer_flags &= ~EHCI_XFER_ABORTWAIT;
		wakeup(&ex->ehci_xfer_flags);
	}
	usb_transfer_complete(xfer);

	splx(s);
}

void
ehci_abort_isoc_xfer(struct usbd_xfer *xfer, usbd_status status)
{
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;
	ehci_isoc_trans_t trans_status;
	struct ehci_soft_itd *itd;
	int i;

	splsoftassert(IPL_SOFTUSB);

	if (sc->sc_bus.dying || xfer->status == USBD_NOT_STARTED) {
		if (xfer->status != USBD_NOT_STARTED)
			TAILQ_REMOVE(&sc->sc_intrhead, ex, inext);
		xfer->status = status;
		timeout_del(&xfer->timeout_handle);
		usb_rem_task(xfer->device, &xfer->abort_task);
		usb_transfer_complete(xfer);
		return;
	}

	/* Transfer is already done. */
	if (xfer->status != USBD_IN_PROGRESS) {
		DPRINTF(("%s: already done \n", __func__));
		return;
	}


#ifdef DIAGNOSTIC
	ex->isdone = 1;
#endif
	xfer->status = status;
	TAILQ_REMOVE(&sc->sc_intrhead, ex, inext);
	timeout_del(&xfer->timeout_handle);
	usb_rem_task(xfer->device, &xfer->abort_task);

	if (xfer->device->speed == USB_SPEED_HIGH) {
		for (itd = ex->itdstart; itd != NULL; itd = itd->xfer_next) {
			usb_syncmem(&itd->dma,
			    itd->offs + offsetof(struct ehci_itd, itd_ctl),
			    sizeof(itd->itd.itd_ctl),
			    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);

			for (i = 0; i < 8; i++) {
				trans_status = le32toh(itd->itd.itd_ctl[i]);
				trans_status &= ~EHCI_ITD_ACTIVE;
				itd->itd.itd_ctl[i] = htole32(trans_status);
			}

			usb_syncmem(&itd->dma,
			    itd->offs + offsetof(struct ehci_itd, itd_ctl),
			    sizeof(itd->itd.itd_ctl),
			    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
		}
	} else {
		for (itd = ex->itdstart; itd != NULL; itd = itd->xfer_next) {
			usb_syncmem(&itd->dma,
			    itd->offs + offsetof(struct ehci_sitd, sitd_trans),
			    sizeof(itd->sitd.sitd_trans),
			    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);

			trans_status = le32toh(itd->sitd.sitd_trans);
			trans_status &= ~EHCI_SITD_ACTIVE;
			itd->sitd.sitd_trans = htole32(trans_status);

			usb_syncmem(&itd->dma,
			    itd->offs + offsetof(struct ehci_sitd, sitd_trans),
			    sizeof(itd->sitd.sitd_trans),
			    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
		}
	}

	sc->sc_softwake = 1;
	usb_schedsoftintr(&sc->sc_bus);
	tsleep_nsec(&sc->sc_softwake, PZERO, "ehciab", INFSLP);

	usb_transfer_complete(xfer);
}

void
ehci_timeout(void *addr)
{
	struct usbd_xfer *xfer = addr;
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;

	if (sc->sc_bus.dying) {
		ehci_timeout_task(addr);
		return;
	}

	usb_init_task(&xfer->abort_task, ehci_timeout_task, addr,
	    USB_TASK_TYPE_ABORT);
	usb_add_task(xfer->device, &xfer->abort_task);
}

void
ehci_timeout_task(void *addr)
{
	struct usbd_xfer *xfer = addr;
	int s;

	s = splusb();
	ehci_abort_xfer(xfer, USBD_TIMEOUT);
	splx(s);
}

/*
 * Some EHCI chips from VIA / ATI seem to trigger interrupts before writing
 * back the qTD status, or miss signalling occasionally under heavy load.
 * If the host machine is too fast, we can miss transaction completion - when
 * we scan the active list the transaction still seems to be active. This
 * generally exhibits itself as a umass stall that never recovers.
 *
 * We work around this behaviour by setting up this callback after any softintr
 * that completes with transactions still pending, giving us another chance to
 * check for completion after the writeback has taken place.
 */
void
ehci_intrlist_timeout(void *arg)
{
	struct ehci_softc *sc = arg;
	int s;

	if (sc->sc_bus.dying)
		return;

	s = splusb();
	DPRINTFN(1, ("ehci_intrlist_timeout\n"));
	usb_schedsoftintr(&sc->sc_bus);
	splx(s);
}

usbd_status
ehci_device_ctrl_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	/* Insert last in queue. */
	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	/* Pipe isn't running, start first */
	return (ehci_device_ctrl_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
ehci_device_ctrl_start(struct usbd_xfer *xfer)
{
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;
	struct ehci_pipe *epipe = (struct ehci_pipe *)xfer->pipe;
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;
	usb_device_request_t *req = &xfer->request;
	struct ehci_soft_qtd *setup, *stat, *next;
	struct ehci_soft_qh *sqh;
	u_int len = UGETW(req->wLength);
	usbd_status err;
	int s;

	KASSERT(xfer->rqflags & URQ_REQUEST);

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	setup = ehci_alloc_sqtd(sc);
	if (setup == NULL) {
		err = USBD_NOMEM;
		goto bad1;
	}
	stat = ehci_alloc_sqtd(sc);
	if (stat == NULL) {
		err = USBD_NOMEM;
		goto bad2;
	}

	sqh = epipe->sqh;

	/* Set up data transaction */
	if (len != 0) {
		struct ehci_soft_qtd *end;

		err = ehci_alloc_sqtd_chain(sc, len, xfer, &next, &end);
		if (err)
			goto bad3;
		end->qtd.qtd_status &= htole32(~EHCI_QTD_IOC);
		end->nextqtd = stat;
		end->qtd.qtd_next =
		    end->qtd.qtd_altnext = htole32(stat->physaddr);
		usb_syncmem(&end->dma, end->offs, sizeof(end->qtd),
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	} else {
		next = stat;
	}

	memcpy(KERNADDR(&epipe->u.ctl.reqdma, 0), req, sizeof(*req));
	usb_syncmem(&epipe->u.ctl.reqdma, 0, sizeof *req, BUS_DMASYNC_PREWRITE);

	/* Clear toggle */
	setup->qtd.qtd_status = htole32(
	    EHCI_QTD_ACTIVE |
	    EHCI_QTD_SET_PID(EHCI_QTD_PID_SETUP) |
	    EHCI_QTD_SET_CERR(3) |
	    EHCI_QTD_SET_TOGGLE(0) |
	    EHCI_QTD_SET_BYTES(sizeof(*req)));
	setup->qtd.qtd_buffer[0] = htole32(DMAADDR(&epipe->u.ctl.reqdma, 0));
	setup->qtd.qtd_buffer_hi[0] = 0;
	setup->nextqtd = next;
	setup->qtd.qtd_next = setup->qtd.qtd_altnext = htole32(next->physaddr);
	setup->len = sizeof(*req);
	usb_syncmem(&setup->dma, setup->offs, sizeof(setup->qtd),
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	stat->qtd.qtd_status = htole32(
	    EHCI_QTD_ACTIVE |
	    EHCI_QTD_SET_PID(usbd_xfer_isread(xfer) ?
		EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN) |
	    EHCI_QTD_SET_CERR(3) |
	    EHCI_QTD_SET_TOGGLE(1) |
	    EHCI_QTD_IOC);
	stat->qtd.qtd_buffer[0] = 0; /* XXX not needed? */
	stat->qtd.qtd_buffer_hi[0] = 0; /* XXX not needed? */
	stat->nextqtd = NULL;
	stat->qtd.qtd_next = stat->qtd.qtd_altnext = htole32(EHCI_LINK_TERMINATE);
	stat->len = 0;
	usb_syncmem(&stat->dma, stat->offs, sizeof(stat->qtd),
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);

	ex->sqtdstart = setup;
	ex->sqtdend = stat;
#ifdef DIAGNOSTIC
	if (!ex->isdone) {
		printf("%s: not done, ex=%p\n", __func__, ex);
	}
	ex->isdone = 0;
#endif

	/* Insert qTD in QH list. */
	s = splusb();
	ehci_set_qh_qtd(sqh, setup);
	if (xfer->timeout && !sc->sc_bus.use_polling) {
		timeout_del(&xfer->timeout_handle);
		timeout_set(&xfer->timeout_handle, ehci_timeout, xfer);
		timeout_add_msec(&xfer->timeout_handle, xfer->timeout);
	}
	TAILQ_INSERT_TAIL(&sc->sc_intrhead, ex, inext);
	xfer->status = USBD_IN_PROGRESS;
	splx(s);

	return (USBD_IN_PROGRESS);

 bad3:
	ehci_free_sqtd(sc, stat);
 bad2:
	ehci_free_sqtd(sc, setup);
 bad1:
	xfer->status = err;
	usb_transfer_complete(xfer);
	return (err);
}

void
ehci_device_ctrl_done(struct usbd_xfer *xfer)
{
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;

	KASSERT(xfer->rqflags & URQ_REQUEST);

	if (xfer->status != USBD_NOMEM) {
		ehci_free_sqtd_chain(sc, ex);
	}
}

void
ehci_device_ctrl_abort(struct usbd_xfer *xfer)
{
	ehci_abort_xfer(xfer, USBD_CANCELLED);
}

void
ehci_device_ctrl_close(struct usbd_pipe *pipe)
{
	ehci_close_pipe(pipe);
}

usbd_status
ehci_device_bulk_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	/* Insert last in queue. */
	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	/* Pipe isn't running, start first */
	return (ehci_device_bulk_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
ehci_device_bulk_start(struct usbd_xfer *xfer)
{
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;
	struct ehci_pipe *epipe = (struct ehci_pipe *)xfer->pipe;
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;
	struct ehci_soft_qtd *data, *dataend;
	struct ehci_soft_qh *sqh;
	usbd_status err;
	int s;

	KASSERT(!(xfer->rqflags & URQ_REQUEST));

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	sqh = epipe->sqh;

	err = ehci_alloc_sqtd_chain(sc, xfer->length, xfer, &data, &dataend);
	if (err) {
		xfer->status = err;
		usb_transfer_complete(xfer);
		return (err);
	}

	/* Set up interrupt info. */
	ex->sqtdstart = data;
	ex->sqtdend = dataend;
#ifdef DIAGNOSTIC
	if (!ex->isdone) {
		printf("ehci_device_bulk_start: not done, ex=%p\n", ex);
	}
	ex->isdone = 0;
#endif

	s = splusb();
	ehci_set_qh_qtd(sqh, data);
	if (xfer->timeout && !sc->sc_bus.use_polling) {
		timeout_del(&xfer->timeout_handle);
		timeout_set(&xfer->timeout_handle, ehci_timeout, xfer);
		timeout_add_msec(&xfer->timeout_handle, xfer->timeout);
	}
	TAILQ_INSERT_TAIL(&sc->sc_intrhead, ex, inext);
	xfer->status = USBD_IN_PROGRESS;
	splx(s);

	return (USBD_IN_PROGRESS);
}

void
ehci_device_bulk_abort(struct usbd_xfer *xfer)
{
	ehci_abort_xfer(xfer, USBD_CANCELLED);
}

/*
 * Close a device bulk pipe.
 */
void
ehci_device_bulk_close(struct usbd_pipe *pipe)
{
	ehci_close_pipe(pipe);
}

void
ehci_device_bulk_done(struct usbd_xfer *xfer)
{
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;

	if (xfer->status != USBD_NOMEM) {
		ehci_free_sqtd_chain(sc, ex);
	}
}

usbd_status
ehci_device_setintr(struct ehci_softc *sc, struct ehci_soft_qh *sqh, int ival)
{
	struct ehci_soft_islot *isp;
	int islot, lev;

	/* Find a poll rate that is large enough. */
	for (lev = EHCI_IPOLLRATES - 1; lev > 0; lev--)
		if (EHCI_ILEV_IVAL(lev) <= ival)
			break;

	/* Pick an interrupt slot at the right level. */
	/* XXX could do better than picking at random */
	islot = EHCI_IQHIDX(lev, arc4random());

	sqh->islot = islot;
	isp = &sc->sc_islots[islot];
	ehci_add_qh(sqh, isp->sqh);

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
ehci_device_intr_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	/* Insert last in queue. */
	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	/*
	 * Pipe isn't running (otherwise err would be USBD_INPROG),
	 * so start it first.
	 */
	return (ehci_device_intr_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
ehci_device_intr_start(struct usbd_xfer *xfer)
{
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;
	struct ehci_pipe *epipe = (struct ehci_pipe *)xfer->pipe;
	struct ehci_soft_qtd *data, *dataend;
	struct ehci_soft_qh *sqh;
	usbd_status err;
	int s;

	KASSERT(!(xfer->rqflags & URQ_REQUEST));

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	sqh = epipe->sqh;

	err = ehci_alloc_sqtd_chain(sc, xfer->length, xfer, &data, &dataend);
	if (err) {
		xfer->status = err;
		usb_transfer_complete(xfer);
		return (err);
	}

	/* Set up interrupt info. */
	ex->sqtdstart = data;
	ex->sqtdend = dataend;
#ifdef DIAGNOSTIC
	if (!ex->isdone)
		printf("ehci_device_intr_start: not done, ex=%p\n", ex);
	ex->isdone = 0;
#endif

	s = splusb();
	ehci_set_qh_qtd(sqh, data);
	if (xfer->timeout && !sc->sc_bus.use_polling) {
		timeout_del(&xfer->timeout_handle);
		timeout_set(&xfer->timeout_handle, ehci_timeout, xfer);
		timeout_add_msec(&xfer->timeout_handle, xfer->timeout);
	}
	TAILQ_INSERT_TAIL(&sc->sc_intrhead, ex, inext);
	xfer->status = USBD_IN_PROGRESS;
	splx(s);

	return (USBD_IN_PROGRESS);
}

void
ehci_device_intr_abort(struct usbd_xfer *xfer)
{
	KASSERT(!xfer->pipe->repeat || xfer->pipe->intrxfer == xfer);

	/*
	 * XXX - abort_xfer uses ehci_sync_hc, which syncs via the advance
	 *       async doorbell. That's dependant on the async list, whereas
	 *       intr xfers are periodic, should not use this?
	 */
	ehci_abort_xfer(xfer, USBD_CANCELLED);
}

void
ehci_device_intr_close(struct usbd_pipe *pipe)
{
	ehci_close_pipe(pipe);
}

void
ehci_device_intr_done(struct usbd_xfer *xfer)
{
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;
	struct ehci_pipe *epipe = (struct ehci_pipe *)xfer->pipe;
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;
	struct ehci_soft_qtd *data, *dataend;
	struct ehci_soft_qh *sqh;
	usbd_status err;
	int s;

	if (xfer->pipe->repeat) {
		ehci_free_sqtd_chain(sc, ex);

		sqh = epipe->sqh;

		err = ehci_alloc_sqtd_chain(sc, xfer->length, xfer, &data, &dataend);
		if (err) {
			xfer->status = err;
			return;
		}

		/* Set up interrupt info. */
		ex->sqtdstart = data;
		ex->sqtdend = dataend;
#ifdef DIAGNOSTIC
		if (!ex->isdone) {
			printf("ehci_device_intr_done: not done, ex=%p\n",
					ex);
		}
		ex->isdone = 0;
#endif

		s = splusb();
		ehci_set_qh_qtd(sqh, data);
		if (xfer->timeout && !sc->sc_bus.use_polling) {
			timeout_del(&xfer->timeout_handle);
			timeout_set(&xfer->timeout_handle, ehci_timeout, xfer);
			timeout_add_msec(&xfer->timeout_handle, xfer->timeout);
		}
		TAILQ_INSERT_TAIL(&sc->sc_intrhead, ex, inext);
		xfer->status = USBD_IN_PROGRESS;
		splx(s);
	} else if (xfer->status != USBD_NOMEM) {
		ehci_free_sqtd_chain(sc, ex);
	}
}

usbd_status
ehci_device_isoc_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	err = usb_insert_transfer(xfer);
	if (err && err != USBD_IN_PROGRESS)
		return (err);

	return (ehci_device_isoc_start(xfer));
}

usbd_status
ehci_device_isoc_start(struct usbd_xfer *xfer)
{
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;
	struct ehci_pipe *epipe = (struct ehci_pipe *)xfer->pipe;
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;
	usb_endpoint_descriptor_t *ed = xfer->pipe->endpoint->edesc;
	uint8_t ival = ed->bInterval;
	struct ehci_soft_itd *itd;
	int s, frindex;
	uint32_t link;

	KASSERT(!(xfer->rqflags & URQ_REQUEST));
	KASSERT(ival > 0 && ival <= 16);

	/*
	 * To allow continuous transfers, above we start all transfers
	 * immediately. However, we're still going to get usbd_start_next call
	 * this when another xfer completes. So, check if this is already
	 * in progress or not
	 */
	if (ex->itdstart != NULL)
		return (USBD_IN_PROGRESS);

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	/* Why would you do that anyway? */
	if (sc->sc_bus.use_polling)
		return (USBD_INVAL);

	/*
	 * To avoid complication, don't allow a request right now that'll span
	 * the entire frame table. To within 4 frames, to allow some leeway
	 * on either side of where the hc currently is.
	 */
	if ((1 << (ival - 1)) * xfer->nframes >= (sc->sc_flsize - 4) * 8)
		return (USBD_INVAL);

	/*
	 * Step 1: Allocate and initialize itds.
	 */
	if (xfer->device->speed == USB_SPEED_HIGH) {
		if (ehci_alloc_itd_chain(sc, xfer))
			return (USBD_INVAL);

		link = EHCI_LINK_ITD;
	} else {
		if (ehci_alloc_sitd_chain(sc, xfer))
			return (USBD_INVAL);

		link = EHCI_LINK_SITD;
	}

#ifdef DIAGNOSTIC
	if (!ex->isdone) {
		printf("%s: not done, ex=%p\n", __func__, ex);
	}
	ex->isdone = 0;
#endif

	/*
	 * Part 2: Transfer descriptors have now been set up, now they must
	 * be scheduled into the period frame list. Erk. Not wanting to
	 * complicate matters, transfer is denied if the transfer spans
	 * more than the period frame list.
	 */
	s = splusb();

	/* Start inserting frames */
	if (epipe->u.isoc.cur_xfers > 0) {
		frindex = epipe->u.isoc.next_frame;
	} else {
		frindex = EOREAD4(sc, EHCI_FRINDEX);
		frindex = frindex >> 3; /* Erase microframe index */
		frindex += 2;
	}

	if (frindex >= sc->sc_flsize)
		frindex &= (sc->sc_flsize - 1);

	/* What's the frame interval? */
	ival = (1 << (ival - 1));
	if (ival / 8 == 0)
		ival = 1;
	else
		ival /= 8;

	/* Abuse the fact that itd_next == sitd_next. */
	for (itd = ex->itdstart; itd != NULL; itd = itd->xfer_next) {
		itd->itd.itd_next = sc->sc_flist[frindex];
		if (itd->itd.itd_next == 0)
			itd->itd.itd_next = htole32(EHCI_LINK_TERMINATE);

		sc->sc_flist[frindex] = htole32(link | itd->physaddr);
		itd->u.frame_list.next = sc->sc_softitds[frindex];
		sc->sc_softitds[frindex] = itd;
		if (itd->u.frame_list.next != NULL)
			itd->u.frame_list.next->u.frame_list.prev = itd;
		itd->slot = frindex;
		itd->u.frame_list.prev = NULL;

		frindex += ival;
		if (frindex >= sc->sc_flsize)
			frindex -= sc->sc_flsize;
	}

	epipe->u.isoc.cur_xfers++;
	epipe->u.isoc.next_frame = frindex;

	TAILQ_INSERT_TAIL(&sc->sc_intrhead, ex, inext);
	xfer->status = USBD_IN_PROGRESS;
	xfer->done = 0;
	splx(s);

	return (USBD_IN_PROGRESS);
}

int
ehci_alloc_itd_chain(struct ehci_softc *sc, struct usbd_xfer *xfer)
{
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;
	usb_endpoint_descriptor_t *ed = xfer->pipe->endpoint->edesc;
	const uint32_t mps = UGETW(ed->wMaxPacketSize);
	struct ehci_soft_itd *itd = NULL, *pitd = NULL;
	int i, j, nframes, uframes, ufrperframe;
	int offs = 0, trans_count = 0;

	/*
	 * How many itds do we need?  One per transfer if interval >= 8
	 * microframes, fewer if we use multiple microframes per frame.
	 */
	switch (ed->bInterval) {
	case 1:
		ufrperframe = 8;
		break;
	case 2:
		ufrperframe = 4;
		break;
	case 3:
		ufrperframe = 2;
		break;
	default:
		ufrperframe = 1;
		break;
	}
	nframes = (xfer->nframes + (ufrperframe - 1)) / ufrperframe;
	uframes = 8 / ufrperframe;
	if (nframes == 0)
		return (1);

	usb_syncmem(&xfer->dmabuf, 0, xfer->length,
	    usbd_xfer_isread(xfer) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);
	for (i = 0; i < nframes; i++) {
		uint32_t froffs = offs;

		itd = ehci_alloc_itd(sc);
		if (itd == NULL) {
			ehci_free_itd_chain(sc, ex);
			return (1);
		}

		if (pitd != NULL)
			pitd->xfer_next = itd;
		else
			ex->itdstart = itd;

		/*
		 * Step 1.5, initialize uframes
		 */
		for (j = 0; j < 8; j += uframes) {
			/* Calculate which page in the list this starts in */
			int addr = DMAADDR(&xfer->dmabuf, froffs);
			addr = EHCI_PAGE_OFFSET(addr) + (offs - froffs);
			addr = EHCI_PAGE(addr) / EHCI_PAGE_SIZE;

			/* This gets the initial offset into the first page,
			 * looks how far further along the current uframe
			 * offset is. Works out how many pages that is.
			 */
			itd->itd.itd_ctl[j] = htole32(
			    EHCI_ITD_ACTIVE |
			    EHCI_ITD_SET_LEN(xfer->frlengths[trans_count]) |
			    EHCI_ITD_SET_PG(addr) |
			    EHCI_ITD_SET_OFFS(DMAADDR(&xfer->dmabuf, offs))
			);

			offs += xfer->frlengths[trans_count];
			trans_count++;

			if (trans_count >= xfer->nframes) { /*Set IOC*/
				itd->itd.itd_ctl[j] |= htole32(EHCI_ITD_IOC);
				break;
			}
		}

		/* Step 1.75, set buffer pointers. To simplify matters, all
		 * pointers are filled out for the next 7 hardware pages in
		 * the dma block, so no need to worry what pages to cover
		 * and what to not.
		 */

		for (j = 0; j < 7; j++) {
			/*
			 * Don't try to lookup a page that's past the end
			 * of buffer
			 */
			int page_offs = EHCI_PAGE(froffs +
			    (EHCI_PAGE_SIZE * j));

			if (page_offs >= xfer->dmabuf.block->size)
				break;

			long long page = DMAADDR(&xfer->dmabuf, page_offs);
			page = EHCI_PAGE(page);
			itd->itd.itd_bufr[j] = htole32(page);
			itd->itd.itd_bufr_hi[j] = htole32(page >> 32);
		}

		/*
		 * Other special values
		 */
		itd->itd.itd_bufr[0] |= htole32(
		    EHCI_ITD_SET_ENDPT(UE_GET_ADDR(ed->bEndpointAddress)) |
		    EHCI_ITD_SET_DADDR(xfer->pipe->device->address)
		);

		itd->itd.itd_bufr[1] |= htole32(
		    (usbd_xfer_isread(xfer) ? EHCI_ITD_SET_DIR(1) : 0) |
		    EHCI_ITD_SET_MAXPKT(UE_GET_SIZE(mps))
		);
		/* FIXME: handle invalid trans */
		itd->itd.itd_bufr[2] |= htole32(
		    EHCI_ITD_SET_MULTI(UE_GET_TRANS(mps)+1)
		);

		pitd = itd;
	}

	ex->itdend = itd;

	return (0);
}

int
ehci_alloc_sitd_chain(struct ehci_softc *sc, struct usbd_xfer *xfer)
{
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;
	struct usbd_device *hshub = xfer->device->myhsport->parent;
	usb_endpoint_descriptor_t *ed = xfer->pipe->endpoint->edesc;
	struct ehci_soft_itd *itd = NULL, *pitd = NULL;
	uint8_t smask, cmask, tp, uf;
	int i, nframes, offs = 0;
	uint32_t endp;

	nframes = xfer->nframes;
	if (nframes == 0)
		return (1);

	endp = EHCI_SITD_SET_ENDPT(UE_GET_ADDR(ed->bEndpointAddress)) |
	    EHCI_SITD_SET_ADDR(xfer->device->address) |
	    EHCI_SITD_SET_PORT(xfer->device->myhsport->portno) |
	    EHCI_SITD_SET_HUBA(hshub->address);

	if (usbd_xfer_isread(xfer))
		endp |= EHCI_SITD_SET_DIR(1);

	usb_syncmem(&xfer->dmabuf, 0, xfer->length,
	    usbd_xfer_isread(xfer) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);
	for (i = 0; i < nframes; i++) {
		uint32_t addr = DMAADDR(&xfer->dmabuf, offs);
		uint32_t page = EHCI_PAGE(addr + xfer->frlengths[i] - 1);

		itd = ehci_alloc_itd(sc);
		if (itd == NULL) {
			ehci_free_itd_chain(sc, ex);
			return (1);
		}
		if (pitd)
			pitd->xfer_next = itd;
		else
			ex->itdstart = itd;

		itd->sitd.sitd_endp = htole32(endp);
		itd->sitd.sitd_back = htole32(EHCI_LINK_TERMINATE);
		itd->sitd.sitd_trans = htole32(
		    EHCI_SITD_ACTIVE |
		    EHCI_SITD_SET_LEN(xfer->frlengths[i]) |
		    ((i == nframes - 1) ? EHCI_SITD_IOC : 0)
		);

		uf = max(1, ((xfer->frlengths[i] + 187) / 188));

		/*
		 * Since we do not yet budget and schedule micro-frames
		 * we assume there is no other transfer using the same
		 * TT.
		 */
		if (usbd_xfer_isread(xfer)) {
			smask = 0x01;
			cmask = ((1 << (uf + 2)) - 1) << 2;
		} else {
			/* Is the payload is greater than 188 bytes? */
			if (uf == 1)
				tp = EHCI_SITD_TP_ALL;
			else
				tp = EHCI_SITD_TP_BEGIN;

			page |=	EHCI_SITD_SET_TCOUNT(uf) | EHCI_SITD_SET_TP(tp);
			smask = (1 << uf) - 1;
			cmask = 0x00;
		}

		itd->sitd.sitd_sched = htole32(
		    EHCI_SITD_SET_SMASK(smask) | EHCI_SITD_SET_CMASK(cmask)
		);
		itd->sitd.sitd_bufr[0] = htole32(addr);
		itd->sitd.sitd_bufr[1] = htole32(page);

		offs += xfer->frlengths[i];
		pitd = itd;
	}

	ex->itdend = itd;

	return (0);
}

void
ehci_device_isoc_abort(struct usbd_xfer *xfer)
{
	int s;

	s = splusb();
	ehci_abort_isoc_xfer(xfer, USBD_CANCELLED);
	splx(s);
}

void
ehci_device_isoc_close(struct usbd_pipe *pipe)
{
}

void
ehci_device_isoc_done(struct usbd_xfer *xfer)
{
	struct ehci_softc *sc = (struct ehci_softc *)xfer->device->bus;
	struct ehci_pipe *epipe = (struct ehci_pipe *)xfer->pipe;
	struct ehci_xfer *ex = (struct ehci_xfer *)xfer;
	int s;

	s = splusb();
	epipe->u.isoc.cur_xfers--;
	if (xfer->status != USBD_NOMEM) {
		ehci_rem_itd_chain(sc, ex);
		ehci_free_itd_chain(sc, ex);
	}
	splx(s);
}
