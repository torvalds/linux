/*	$OpenBSD: uhci.c,v 1.156 2022/04/12 19:41:11 naddy Exp $	*/
/*	$NetBSD: uhci.c,v 1.172 2003/02/23 04:19:26 simonb Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/uhci.c,v 1.33 1999/11/17 22:33:41 n_hibma Exp $	*/

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
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/timeout.h>
#include <sys/pool.h>
#include <sys/endian.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#include <dev/usb/uhcireg.h>
#include <dev/usb/uhcivar.h>

/* Use bandwidth reclamation for control transfers. Some devices choke on it. */
/*#define UHCI_CTL_LOOP */

struct cfdriver uhci_cd = {
	NULL, "uhci", DV_DULL, CD_SKIPHIBERNATE
};

#ifdef UHCI_DEBUG
struct uhci_softc *thesc;
#define DPRINTF(x)	if (uhcidebug) printf x
#define DPRINTFN(n,x)	if (uhcidebug>(n)) printf x
int uhcidebug = 0;
int uhcinoloop = 0;
#define bitmask_snprintf(q,f,b,l) snprintf((b), (l), "%b", (q), (f))
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

struct pool *uhcixfer;

struct uhci_pipe {
	struct usbd_pipe pipe;
	int nexttoggle;

	union {
		/* Control pipe */
		struct {
			struct uhci_soft_qh *sqh;
			struct usb_dma reqdma;
			struct uhci_soft_td *setup, *stat;
			u_int length;
		} ctl;
		/* Interrupt pipe */
		struct {
			int npoll;
			int isread;
			struct uhci_soft_qh **qhs;
		} intr;
		/* Bulk pipe */
		struct {
			struct uhci_soft_qh *sqh;
			u_int length;
			int isread;
		} bulk;
		/* Iso pipe */
		struct iso {
			struct uhci_soft_td **stds;
			int next, inuse;
		} iso;
	} u;
};

void		uhci_globalreset(struct uhci_softc *);
usbd_status	uhci_portreset(struct uhci_softc *, int);
void		uhci_reset(struct uhci_softc *);
usbd_status	uhci_run(struct uhci_softc *, int run);
struct uhci_soft_td *uhci_alloc_std(struct uhci_softc *);
void		uhci_free_std(struct uhci_softc *, struct uhci_soft_td *);
struct uhci_soft_qh *uhci_alloc_sqh(struct uhci_softc *);
void		uhci_free_sqh(struct uhci_softc *, struct uhci_soft_qh *);

void		uhci_free_std_chain(struct uhci_softc *,
		    struct uhci_soft_td *, struct uhci_soft_td *);
usbd_status	uhci_alloc_std_chain(struct uhci_softc *, u_int,
		    struct usbd_xfer *, struct uhci_soft_td **,
		    struct uhci_soft_td **);
void		uhci_poll_hub(void *);
void		uhci_check_intr(struct uhci_softc *, struct usbd_xfer *);
void		uhci_idone(struct usbd_xfer *);

void		uhci_abort_xfer(struct usbd_xfer *, usbd_status status);

void		uhci_timeout(void *);
void		uhci_timeout_task(void *);
void		uhci_add_ls_ctrl(struct uhci_softc *, struct uhci_soft_qh *);
void		uhci_add_hs_ctrl(struct uhci_softc *, struct uhci_soft_qh *);
void		uhci_add_bulk(struct uhci_softc *, struct uhci_soft_qh *);
void		uhci_remove_ls_ctrl(struct uhci_softc *, struct uhci_soft_qh *);
void		uhci_remove_hs_ctrl(struct uhci_softc *, struct uhci_soft_qh *);
void		uhci_remove_bulk(struct uhci_softc *,struct uhci_soft_qh *);
void		uhci_add_loop(struct uhci_softc *sc);
void		uhci_rem_loop(struct uhci_softc *sc);

usbd_status	uhci_setup_isoc(struct usbd_pipe *pipe);
void		uhci_device_isoc_enter(struct usbd_xfer *);

struct usbd_xfer *uhci_allocx(struct usbd_bus *);
void		uhci_freex(struct usbd_bus *, struct usbd_xfer *);

usbd_status	uhci_device_ctrl_transfer(struct usbd_xfer *);
usbd_status	uhci_device_ctrl_start(struct usbd_xfer *);
void		uhci_device_ctrl_abort(struct usbd_xfer *);
void		uhci_device_ctrl_close(struct usbd_pipe *);
void		uhci_device_ctrl_done(struct usbd_xfer *);

usbd_status	uhci_device_intr_transfer(struct usbd_xfer *);
usbd_status	uhci_device_intr_start(struct usbd_xfer *);
void		uhci_device_intr_abort(struct usbd_xfer *);
void		uhci_device_intr_close(struct usbd_pipe *);
void		uhci_device_intr_done(struct usbd_xfer *);

usbd_status	uhci_device_bulk_transfer(struct usbd_xfer *);
usbd_status	uhci_device_bulk_start(struct usbd_xfer *);
void		uhci_device_bulk_abort(struct usbd_xfer *);
void		uhci_device_bulk_close(struct usbd_pipe *);
void		uhci_device_bulk_done(struct usbd_xfer *);

usbd_status	uhci_device_isoc_transfer(struct usbd_xfer *);
usbd_status	uhci_device_isoc_start(struct usbd_xfer *);
void		uhci_device_isoc_abort(struct usbd_xfer *);
void		uhci_device_isoc_close(struct usbd_pipe *);
void		uhci_device_isoc_done(struct usbd_xfer *);

usbd_status	uhci_root_ctrl_transfer(struct usbd_xfer *);
usbd_status	uhci_root_ctrl_start(struct usbd_xfer *);
void		uhci_root_ctrl_abort(struct usbd_xfer *);
void		uhci_root_ctrl_close(struct usbd_pipe *);
void		uhci_root_ctrl_done(struct usbd_xfer *);

usbd_status	uhci_root_intr_transfer(struct usbd_xfer *);
usbd_status	uhci_root_intr_start(struct usbd_xfer *);
void		uhci_root_intr_abort(struct usbd_xfer *);
void		uhci_root_intr_close(struct usbd_pipe *);
void		uhci_root_intr_done(struct usbd_xfer *);

usbd_status	uhci_open(struct usbd_pipe *);
void		uhci_poll(struct usbd_bus *);
void		uhci_softintr(void *);

usbd_status	uhci_device_request(struct usbd_xfer *xfer);

void		uhci_add_intr(struct uhci_softc *, struct uhci_soft_qh *);
void		uhci_remove_intr(struct uhci_softc *, struct uhci_soft_qh *);
usbd_status	uhci_device_setintr(struct uhci_softc *sc,
		    struct uhci_pipe *pipe, int ival);

void		uhci_device_clear_toggle(struct usbd_pipe *pipe);

static inline struct uhci_soft_qh *uhci_find_prev_qh(struct uhci_soft_qh *,
		    struct uhci_soft_qh *);

#ifdef UHCI_DEBUG
void		uhci_dump_all(struct uhci_softc *);
void		uhci_dumpregs(struct uhci_softc *);
void		uhci_dump_qhs(struct uhci_soft_qh *);
void		uhci_dump_qh(struct uhci_soft_qh *);
void		uhci_dump_tds(struct uhci_soft_td *);
void		uhci_dump_td(struct uhci_soft_td *);
void		uhci_dump_xfer(struct uhci_xfer *);
void		uhci_dump(void);
#endif

#define UBARR(sc) bus_space_barrier((sc)->iot, (sc)->ioh, 0, (sc)->sc_size, \
			BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE)
#define UWRITE1(sc, r, x) \
 do { UBARR(sc); bus_space_write_1((sc)->iot, (sc)->ioh, (r), (x)); \
 } while (/*CONSTCOND*/0)
#define UWRITE2(sc, r, x) \
 do { UBARR(sc); bus_space_write_2((sc)->iot, (sc)->ioh, (r), (x)); \
 } while (/*CONSTCOND*/0)
#define UWRITE4(sc, r, x) \
 do { UBARR(sc); bus_space_write_4((sc)->iot, (sc)->ioh, (r), (x)); \
 } while (/*CONSTCOND*/0)

__unused static __inline u_int8_t
UREAD1(struct uhci_softc *sc, bus_size_t r)
{
	UBARR(sc);
	return bus_space_read_1(sc->iot, sc->ioh, r);
}

__unused static __inline u_int16_t
UREAD2(struct uhci_softc *sc, bus_size_t r)
{
	UBARR(sc);
	return bus_space_read_2(sc->iot, sc->ioh, r);
}

__unused static __inline u_int32_t
UREAD4(struct uhci_softc *sc, bus_size_t r)
{
	UBARR(sc);
	return bus_space_read_4(sc->iot, sc->ioh, r);
}

#define UHCICMD(sc, cmd) UWRITE2(sc, UHCI_CMD, cmd)
#define UHCISTS(sc) UREAD2(sc, UHCI_STS)

#define UHCI_RESET_TIMEOUT 100	/* ms, reset timeout */

#define UHCI_CURFRAME(sc) (UREAD2(sc, UHCI_FRNUM) & UHCI_FRNUM_MASK)

#define UHCI_INTR_ENDPT 1

const struct usbd_bus_methods uhci_bus_methods = {
	.open_pipe = uhci_open,
	.dev_setaddr = usbd_set_address,
	.soft_intr = uhci_softintr,
	.do_poll = uhci_poll,
	.allocx = uhci_allocx,
	.freex = uhci_freex,
};

const struct usbd_pipe_methods uhci_root_ctrl_methods = {
	.transfer = uhci_root_ctrl_transfer,
	.start = uhci_root_ctrl_start,
	.abort = uhci_root_ctrl_abort,
	.close = uhci_root_ctrl_close,
	.done = uhci_root_ctrl_done,
};

const struct usbd_pipe_methods uhci_root_intr_methods = {
	.transfer = uhci_root_intr_transfer,
	.start = uhci_root_intr_start,
	.abort = uhci_root_intr_abort,
	.close = uhci_root_intr_close,
	.done = uhci_root_intr_done,
};

const struct usbd_pipe_methods uhci_device_ctrl_methods = {
	.transfer = uhci_device_ctrl_transfer,
	.start = uhci_device_ctrl_start,
	.abort = uhci_device_ctrl_abort,
	.close = uhci_device_ctrl_close,
	.done = uhci_device_ctrl_done,
};

const struct usbd_pipe_methods uhci_device_intr_methods = {
	.transfer = uhci_device_intr_transfer,
	.start = uhci_device_intr_start,
	.abort = uhci_device_intr_abort,
	.close = uhci_device_intr_close,
	.cleartoggle = uhci_device_clear_toggle,
	.done = uhci_device_intr_done,
};

const struct usbd_pipe_methods uhci_device_bulk_methods = {
	.transfer = uhci_device_bulk_transfer,
	.start = uhci_device_bulk_start,
	.abort = uhci_device_bulk_abort,
	.close = uhci_device_bulk_close,
	.cleartoggle = uhci_device_clear_toggle,
	.done = uhci_device_bulk_done,
};

const struct usbd_pipe_methods uhci_device_isoc_methods = {
	.transfer = uhci_device_isoc_transfer,
	.start = uhci_device_isoc_start,
	.abort = uhci_device_isoc_abort,
	.close = uhci_device_isoc_close,
	.done = uhci_device_isoc_done,
};

#define uhci_add_intr_list(sc, ex) \
	LIST_INSERT_HEAD(&(sc)->sc_intrhead, (ex), inext)
#define uhci_del_intr_list(ex) \
	do { \
		LIST_REMOVE((ex), inext); \
		(ex)->inext.le_prev = NULL; \
	} while (0)
#define uhci_active_intr_list(ex) ((ex)->inext.le_prev != NULL)

static inline struct uhci_soft_qh *
uhci_find_prev_qh(struct uhci_soft_qh *pqh, struct uhci_soft_qh *sqh)
{
	DPRINTFN(15,("uhci_find_prev_qh: pqh=%p sqh=%p\n", pqh, sqh));

	for (; pqh->hlink != sqh; pqh = pqh->hlink) {
#if defined(DIAGNOSTIC) || defined(UHCI_DEBUG)
		if (letoh32(pqh->qh.qh_hlink) & UHCI_PTR_T) {
			printf("uhci_find_prev_qh: QH not found\n");
			return (NULL);
		}
#endif
	}
	return (pqh);
}

void
uhci_globalreset(struct uhci_softc *sc)
{
	UHCICMD(sc, UHCI_CMD_GRESET);	/* global reset */
	usb_delay_ms(&sc->sc_bus, USB_BUS_RESET_DELAY); /* wait a little */
	UHCICMD(sc, 0);			/* do nothing */
}

usbd_status
uhci_init(struct uhci_softc *sc)
{
	usbd_status err;
	int i, j;
	struct uhci_soft_qh *clsqh, *chsqh, *bsqh, *sqh, *lsqh;
	struct uhci_soft_td *std;

	DPRINTFN(1,("uhci_init: start\n"));

#ifdef UHCI_DEBUG
	thesc = sc;

	if (uhcidebug > 2)
		uhci_dumpregs(sc);
#endif

	/* Save SOF over HC reset. */
	sc->sc_saved_sof = UREAD1(sc, UHCI_SOF);

	UWRITE2(sc, UHCI_INTR, 0);		/* disable interrupts */
	uhci_globalreset(sc);			/* reset the controller */
	uhci_reset(sc);

	if (uhcixfer == NULL) {
		uhcixfer = malloc(sizeof(struct pool), M_USBHC, M_NOWAIT);
		if (uhcixfer == NULL) {
			printf("%s: unable to allocate pool descriptor\n",
			    sc->sc_bus.bdev.dv_xname);
			return (ENOMEM);
		}
		pool_init(uhcixfer, sizeof(struct uhci_xfer), 0, IPL_SOFTUSB,
		    0, "uhcixfer", NULL);
	}

	/* Restore saved SOF. */
	UWRITE1(sc, UHCI_SOF, sc->sc_saved_sof);

	/* Allocate and initialize real frame array. */
	err = usb_allocmem(&sc->sc_bus,
		  UHCI_FRAMELIST_COUNT * sizeof(uhci_physaddr_t),
		  UHCI_FRAMELIST_ALIGN, USB_DMA_COHERENT, &sc->sc_dma);
	if (err)
		return (err);
	sc->sc_pframes = KERNADDR(&sc->sc_dma, 0);
	UWRITE2(sc, UHCI_FRNUM, 0);		/* set frame number to 0 */
	UWRITE4(sc, UHCI_FLBASEADDR, DMAADDR(&sc->sc_dma, 0)); /* set frame list*/

	/*
	 * Allocate a TD, inactive, that hangs from the last QH.
	 * This is to avoid a bug in the PIIX that makes it run berserk
	 * otherwise.
	 */
	std = uhci_alloc_std(sc);
	if (std == NULL)
		return (USBD_NOMEM);
	std->link.std = NULL;
	std->td.td_link = htole32(UHCI_PTR_T);
	std->td.td_status = htole32(0); /* inactive */
	std->td.td_token = htole32(0);
	std->td.td_buffer = htole32(0);

	/* Allocate the dummy QH marking the end and used for looping the QHs.*/
	lsqh = uhci_alloc_sqh(sc);
	if (lsqh == NULL)
		return (USBD_NOMEM);
	lsqh->hlink = NULL;
	lsqh->qh.qh_hlink = htole32(UHCI_PTR_T);	/* end of QH chain */
	lsqh->elink = std;
	lsqh->qh.qh_elink = htole32(std->physaddr | UHCI_PTR_TD);
	sc->sc_last_qh = lsqh;

	/* Allocate the dummy QH where bulk traffic will be queued. */
	bsqh = uhci_alloc_sqh(sc);
	if (bsqh == NULL)
		return (USBD_NOMEM);
	bsqh->hlink = lsqh;
	bsqh->qh.qh_hlink = htole32(lsqh->physaddr | UHCI_PTR_QH);
	bsqh->elink = NULL;
	bsqh->qh.qh_elink = htole32(UHCI_PTR_T);
	sc->sc_bulk_start = sc->sc_bulk_end = bsqh;

	/* Allocate dummy QH where high speed control traffic will be queued. */
	chsqh = uhci_alloc_sqh(sc);
	if (chsqh == NULL)
		return (USBD_NOMEM);
	chsqh->hlink = bsqh;
	chsqh->qh.qh_hlink = htole32(bsqh->physaddr | UHCI_PTR_QH);
	chsqh->elink = NULL;
	chsqh->qh.qh_elink = htole32(UHCI_PTR_T);
	sc->sc_hctl_start = sc->sc_hctl_end = chsqh;

	/* Allocate dummy QH where control traffic will be queued. */
	clsqh = uhci_alloc_sqh(sc);
	if (clsqh == NULL)
		return (USBD_NOMEM);
	clsqh->hlink = chsqh;
	clsqh->qh.qh_hlink = htole32(chsqh->physaddr | UHCI_PTR_QH);
	clsqh->elink = NULL;
	clsqh->qh.qh_elink = htole32(UHCI_PTR_T);
	sc->sc_lctl_start = sc->sc_lctl_end = clsqh;

	/*
	 * Make all (virtual) frame list pointers point to the interrupt
	 * queue heads and the interrupt queue heads at the control
	 * queue head and point the physical frame list to the virtual.
	 */
	for(i = 0; i < UHCI_VFRAMELIST_COUNT; i++) {
		std = uhci_alloc_std(sc);
		sqh = uhci_alloc_sqh(sc);
		if (std == NULL || sqh == NULL)
			return (USBD_NOMEM);
		std->link.sqh = sqh;
		std->td.td_link = htole32(sqh->physaddr | UHCI_PTR_QH);
		std->td.td_status = htole32(UHCI_TD_IOS); /* iso, inactive */
		std->td.td_token = htole32(0);
		std->td.td_buffer = htole32(0);
		sqh->hlink = clsqh;
		sqh->qh.qh_hlink = htole32(clsqh->physaddr | UHCI_PTR_QH);
		sqh->elink = NULL;
		sqh->qh.qh_elink = htole32(UHCI_PTR_T);
		sc->sc_vframes[i].htd = std;
		sc->sc_vframes[i].etd = std;
		sc->sc_vframes[i].hqh = sqh;
		sc->sc_vframes[i].eqh = sqh;
		for (j = i;
		     j < UHCI_FRAMELIST_COUNT;
		     j += UHCI_VFRAMELIST_COUNT)
			sc->sc_pframes[j] = htole32(std->physaddr);
	}

	LIST_INIT(&sc->sc_intrhead);

	timeout_set(&sc->sc_root_intr, uhci_poll_hub, sc);

	/* Set up the bus struct. */
	sc->sc_bus.methods = &uhci_bus_methods;
	sc->sc_bus.pipe_size = sizeof(struct uhci_pipe);

	sc->sc_suspend = DVACT_RESUME;

	UHCICMD(sc, UHCI_CMD_MAXP); /* Assume 64 byte packets at frame end */

	DPRINTFN(1,("uhci_init: enabling\n"));
	UWRITE2(sc, UHCI_INTR, UHCI_INTR_TOCRCIE | UHCI_INTR_RIE |
		UHCI_INTR_IOCE | UHCI_INTR_SPIE);	/* enable interrupts */

	return (uhci_run(sc, 1));		/* and here we go... */
}

int
uhci_activate(struct device *self, int act)
{
	struct uhci_softc *sc = (struct uhci_softc *)self;
	int cmd, rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
#ifdef UHCI_DEBUG
		if (uhcidebug > 2)
			uhci_dumpregs(sc);
#endif
		rv = config_activate_children(self, act);
		sc->sc_bus.use_polling++;
		uhci_run(sc, 0); /* stop the controller */

		/* save some state if BIOS doesn't */
		sc->sc_saved_frnum = UREAD2(sc, UHCI_FRNUM);

		UWRITE2(sc, UHCI_INTR, 0); /* disable intrs */

		cmd = UREAD2(sc, UHCI_CMD);
		UHCICMD(sc, cmd | UHCI_CMD_EGSM); /* enter global suspend */
		usb_delay_ms(&sc->sc_bus, USB_RESUME_WAIT);
		sc->sc_suspend = act;
		sc->sc_bus.use_polling--;
		DPRINTF(("uhci_activate: cmd=0x%x\n", UREAD2(sc, UHCI_CMD)));
		break;
	case DVACT_POWERDOWN:
		rv = config_activate_children(self, act);
		uhci_run(sc, 0); /* stop the controller */
		break;
	case DVACT_RESUME:
#ifdef DIAGNOSTIC
		if (sc->sc_suspend == DVACT_RESUME)
			printf("uhci_powerhook: weird, resume without suspend.\n");
#endif
		sc->sc_bus.use_polling++;
		sc->sc_suspend = act;
		cmd = UREAD2(sc, UHCI_CMD);
		if (cmd & UHCI_CMD_RS)
			uhci_run(sc, 0); /* in case BIOS has started it */

		/* restore saved state */
		UWRITE4(sc, UHCI_FLBASEADDR, DMAADDR(&sc->sc_dma, 0));
		UWRITE2(sc, UHCI_FRNUM, sc->sc_saved_frnum);
		UWRITE1(sc, UHCI_SOF, sc->sc_saved_sof);

		UHCICMD(sc, cmd | UHCI_CMD_FGR); /* force global resume */
		usb_delay_ms(&sc->sc_bus, USB_RESUME_DELAY);
		UHCICMD(sc, cmd & ~UHCI_CMD_EGSM); /* back to normal */
		UHCICMD(sc, UHCI_CMD_MAXP);
		UWRITE2(sc, UHCI_INTR, UHCI_INTR_TOCRCIE | UHCI_INTR_RIE |
			UHCI_INTR_IOCE | UHCI_INTR_SPIE); /* re-enable intrs */
		uhci_run(sc, 1); /* and start traffic again */
		usb_delay_ms(&sc->sc_bus, USB_RESUME_RECOVERY);
		sc->sc_bus.use_polling--;
#ifdef UHCI_DEBUG
		if (uhcidebug > 2)
			uhci_dumpregs(sc);
#endif
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

int
uhci_detach(struct device *self, int flags)
{
#ifdef DIAGNOSTIC
	struct uhci_softc *sc = (struct uhci_softc *)self;
#endif
	int rv;

	rv = config_detach_children(self, flags);
	if (rv != 0)
		return (rv);

	KASSERT(sc->sc_intrxfer == NULL);

	/* XXX free other data structures XXX */

	return (rv);
}

struct usbd_xfer *
uhci_allocx(struct usbd_bus *bus)
{
	struct uhci_xfer *ux;

	ux = pool_get(uhcixfer, PR_NOWAIT | PR_ZERO);
#ifdef DIAGNOSTIC
	if (ux != NULL)
		ux->isdone = 1;
#endif
	return ((struct usbd_xfer *)ux);
}

void
uhci_freex(struct usbd_bus *bus, struct usbd_xfer *xfer)
{
	struct uhci_xfer *ux = (struct uhci_xfer*)xfer;

#ifdef DIAGNOSTIC
	if (!ux->isdone) {
		printf("%s: !isdone\n", __func__);
		return;
	}
#endif
	pool_put(uhcixfer, ux);
}

#ifdef UHCI_DEBUG
void
uhci_dumpregs(struct uhci_softc *sc)
{
	DPRINTFN(-1,("%s regs: cmd=%04x, sts=%04x, intr=%04x, frnum=%04x, "
		     "flbase=%08x, sof=%04x, portsc1=%04x, portsc2=%04x\n",
		     sc->sc_bus.bdev.dv_xname,
		     UREAD2(sc, UHCI_CMD),
		     UREAD2(sc, UHCI_STS),
		     UREAD2(sc, UHCI_INTR),
		     UREAD2(sc, UHCI_FRNUM),
		     UREAD4(sc, UHCI_FLBASEADDR),
		     UREAD1(sc, UHCI_SOF),
		     UREAD2(sc, UHCI_PORTSC1),
		     UREAD2(sc, UHCI_PORTSC2)));
}

void
uhci_dump_td(struct uhci_soft_td *p)
{
	char sbuf[128], sbuf2[128];

	DPRINTFN(-1,("TD(%p) at %08lx = link=0x%08lx status=0x%08lx "
		     "token=0x%08lx buffer=0x%08lx\n",
		     p, (long)p->physaddr,
		     (long)letoh32(p->td.td_link),
		     (long)letoh32(p->td.td_status),
		     (long)letoh32(p->td.td_token),
		     (long)letoh32(p->td.td_buffer)));

	bitmask_snprintf((u_int32_t)letoh32(p->td.td_link), "\20\1T\2Q\3VF",
			 sbuf, sizeof(sbuf));
	bitmask_snprintf((u_int32_t)letoh32(p->td.td_status),
			 "\20\22BITSTUFF\23CRCTO\24NAK\25BABBLE\26DBUFFER\27"
			 "STALLED\30ACTIVE\31IOC\32ISO\33LS\36SPD",
			 sbuf2, sizeof(sbuf2));

	DPRINTFN(-1,("  %s %s,errcnt=%d,actlen=%d pid=%02x,addr=%d,endpt=%d,"
		     "D=%d,maxlen=%d\n", sbuf, sbuf2,
		     UHCI_TD_GET_ERRCNT(letoh32(p->td.td_status)),
		     UHCI_TD_GET_ACTLEN(letoh32(p->td.td_status)),
		     UHCI_TD_GET_PID(letoh32(p->td.td_token)),
		     UHCI_TD_GET_DEVADDR(letoh32(p->td.td_token)),
		     UHCI_TD_GET_ENDPT(letoh32(p->td.td_token)),
		     UHCI_TD_GET_DT(letoh32(p->td.td_token)),
		     UHCI_TD_GET_MAXLEN(letoh32(p->td.td_token))));
}

void
uhci_dump_qh(struct uhci_soft_qh *sqh)
{
	DPRINTFN(-1,("QH(%p) at %08x: hlink=%08x elink=%08x\n", sqh,
	    (int)sqh->physaddr, letoh32(sqh->qh.qh_hlink),
	    letoh32(sqh->qh.qh_elink)));
}


void
uhci_dump(void)
{
	uhci_dump_all(thesc);
}

void
uhci_dump_all(struct uhci_softc *sc)
{
	uhci_dumpregs(sc);
	printf("intrs=%d\n", sc->sc_bus.no_intrs);
	/*printf("framelist[i].link = %08x\n", sc->sc_framelist[0].link);*/
	uhci_dump_qh(sc->sc_lctl_start);
}


void
uhci_dump_qhs(struct uhci_soft_qh *sqh)
{
	uhci_dump_qh(sqh);

	/* uhci_dump_qhs displays all the QHs and TDs from the given QH onwards
	 * Traverses sideways first, then down.
	 *
	 * QH1
	 * QH2
	 * No QH
	 * TD2.1
	 * TD2.2
	 * TD1.1
	 * etc.
	 *
	 * TD2.x being the TDs queued at QH2 and QH1 being referenced from QH1.
	 */


	if (sqh->hlink != NULL && !(letoh32(sqh->qh.qh_hlink) & UHCI_PTR_T))
		uhci_dump_qhs(sqh->hlink);
	else
		DPRINTF(("No QH\n"));

	if (sqh->elink != NULL && !(letoh32(sqh->qh.qh_elink) & UHCI_PTR_T))
		uhci_dump_tds(sqh->elink);
	else
		DPRINTF(("No TD\n"));
}

void
uhci_dump_tds(struct uhci_soft_td *std)
{
	struct uhci_soft_td *td;

	for(td = std; td != NULL; td = td->link.std) {
		uhci_dump_td(td);

		/* Check whether the link pointer in this TD marks
		 * the link pointer as end of queue. This avoids
		 * printing the free list in case the queue/TD has
		 * already been moved there (seatbelt).
		 */
		if (letoh32(td->td.td_link) & UHCI_PTR_T ||
		    letoh32(td->td.td_link) == 0)
			break;
	}
}

void
uhci_dump_xfer(struct uhci_xfer *ex)
{
	struct usbd_pipe *pipe;
	usb_endpoint_descriptor_t *ed;
	struct usbd_device *dev;

#ifdef DIAGNOSTIC
#define DONE ex->isdone
#else
#define DONE 0
#endif
        if (ex == NULL) {
                printf("ex NULL\n");
                return;
        }
        pipe = ex->xfer.pipe;
        if (pipe == NULL) {
		printf("ex %p: done=%d pipe=NULL\n",
		       ex, DONE);
                return;
	}
        if (pipe->endpoint == NULL) {
		printf("ex %p: done=%d pipe=%p pipe->endpoint=NULL\n",
		       ex, DONE, pipe);
                return;
	}
        if (pipe->device == NULL) {
		printf("ex %p: done=%d pipe=%p pipe->device=NULL\n",
		       ex, DONE, pipe);
                return;
	}
        ed = pipe->endpoint->edesc;
        dev = pipe->device;
	printf("ex %p: done=%d dev=%p vid=0x%04x pid=0x%04x addr=%d pipe=%p ep=0x%02x attr=0x%02x\n",
	       ex, DONE, dev,
	       UGETW(dev->ddesc.idVendor),
	       UGETW(dev->ddesc.idProduct),
	       dev->address, pipe,
	       ed->bEndpointAddress, ed->bmAttributes);
#undef DONE
}

void uhci_dump_xfers(struct uhci_softc *sc);
void
uhci_dump_xfers(struct uhci_softc *sc)
{
	struct uhci_xfer *ex;

	printf("ex list:\n");
	for (ex = LIST_FIRST(&sc->sc_intrhead); ex; ex = LIST_NEXT(ex, inext))
		uhci_dump_xfer(ex);
}

void exdump(void);
void exdump(void) { uhci_dump_xfers(thesc); }

#endif

/*
 * This routine is executed periodically and simulates interrupts
 * from the root controller interrupt pipe for port status change.
 */
void
uhci_poll_hub(void *addr)
{
	struct uhci_softc *sc = addr;
	struct usbd_xfer *xfer;
	int s;
	u_char *p;

	if (sc->sc_bus.dying)
		return;

	xfer = sc->sc_intrxfer;
	if (xfer == NULL)
		return;

	p = KERNADDR(&xfer->dmabuf, 0);
	p[0] = 0;
	if (UREAD2(sc, UHCI_PORTSC1) & (UHCI_PORTSC_CSC|UHCI_PORTSC_OCIC))
		p[0] |= 1<<1;
	if (UREAD2(sc, UHCI_PORTSC2) & (UHCI_PORTSC_CSC|UHCI_PORTSC_OCIC))
		p[0] |= 1<<2;
	if (p[0] == 0) {
		/* No change, try again in a while */
		timeout_add_msec(&sc->sc_root_intr, 255);
		return;
	}

	xfer->actlen = xfer->length;
	xfer->status = USBD_NORMAL_COMPLETION;

	s = splusb();
	xfer->device->bus->intr_context++;
	usb_transfer_complete(xfer);
	xfer->device->bus->intr_context--;
	splx(s);
}

void
uhci_root_ctrl_done(struct usbd_xfer *xfer)
{
}

/*
 * Let the last QH loop back to the high speed control transfer QH.
 * This is what intel calls "bandwidth reclamation" and improves
 * USB performance a lot for some devices.
 * If we are already looping, just count it.
 */
void
uhci_add_loop(struct uhci_softc *sc) {
#ifdef UHCI_DEBUG
	if (uhcinoloop)
		return;
#endif
	if (++sc->sc_loops == 1) {
		DPRINTFN(5,("uhci_add_loop\n"));
		/* Note, we don't loop back the soft pointer. */
		sc->sc_last_qh->qh.qh_hlink =
		    htole32(sc->sc_hctl_start->physaddr | UHCI_PTR_QH);
	}
}

void
uhci_rem_loop(struct uhci_softc *sc) {
#ifdef UHCI_DEBUG
	if (uhcinoloop)
		return;
#endif
	if (--sc->sc_loops == 0) {
		DPRINTFN(5,("uhci_rem_loop\n"));
		sc->sc_last_qh->qh.qh_hlink = htole32(UHCI_PTR_T);
	}
}

/* Add high speed control QH, called at splusb(). */
void
uhci_add_hs_ctrl(struct uhci_softc *sc, struct uhci_soft_qh *sqh)
{
	struct uhci_soft_qh *eqh;

	splsoftassert(IPL_SOFTUSB);

	DPRINTFN(10, ("uhci_add_hs_ctrl: sqh=%p\n", sqh));
	eqh = sc->sc_hctl_end;
	sqh->hlink       = eqh->hlink;
	sqh->qh.qh_hlink = eqh->qh.qh_hlink;
	eqh->hlink       = sqh;
	eqh->qh.qh_hlink = htole32(sqh->physaddr | UHCI_PTR_QH);
	sc->sc_hctl_end = sqh;
#ifdef UHCI_CTL_LOOP
	uhci_add_loop(sc);
#endif
}

/* Remove high speed control QH, called at splusb(). */
void
uhci_remove_hs_ctrl(struct uhci_softc *sc, struct uhci_soft_qh *sqh)
{
	struct uhci_soft_qh *pqh;

	splsoftassert(IPL_SOFTUSB);

	DPRINTFN(10, ("uhci_remove_hs_ctrl: sqh=%p\n", sqh));
#ifdef UHCI_CTL_LOOP
	uhci_rem_loop(sc);
#endif
	/*
	 * The T bit should be set in the elink of the QH so that the HC
	 * doesn't follow the pointer.  This condition may fail if the
	 * the transferred packet was short so that the QH still points
	 * at the last used TD.
	 * In this case we set the T bit and wait a little for the HC
	 * to stop looking at the TD.
	 */
	if (!(sqh->qh.qh_elink & htole32(UHCI_PTR_T))) {
		sqh->qh.qh_elink = htole32(UHCI_PTR_T);
		delay(UHCI_QH_REMOVE_DELAY);
	}

	pqh = uhci_find_prev_qh(sc->sc_hctl_start, sqh);
	pqh->hlink = sqh->hlink;
	pqh->qh.qh_hlink = sqh->qh.qh_hlink;
	delay(UHCI_QH_REMOVE_DELAY);
	if (sc->sc_hctl_end == sqh)
		sc->sc_hctl_end = pqh;
}

/* Add low speed control QH, called at splusb(). */
void
uhci_add_ls_ctrl(struct uhci_softc *sc, struct uhci_soft_qh *sqh)
{
	struct uhci_soft_qh *eqh;

	splsoftassert(IPL_SOFTUSB);

	DPRINTFN(10, ("uhci_add_ls_ctrl: sqh=%p\n", sqh));
	eqh = sc->sc_lctl_end;
	sqh->hlink = eqh->hlink;
	sqh->qh.qh_hlink = eqh->qh.qh_hlink;
	eqh->hlink = sqh;
	eqh->qh.qh_hlink = htole32(sqh->physaddr | UHCI_PTR_QH);
	sc->sc_lctl_end = sqh;
}

/* Remove low speed control QH, called at splusb(). */
void
uhci_remove_ls_ctrl(struct uhci_softc *sc, struct uhci_soft_qh *sqh)
{
	struct uhci_soft_qh *pqh;

	splsoftassert(IPL_SOFTUSB);

	DPRINTFN(10, ("uhci_remove_ls_ctrl: sqh=%p\n", sqh));
	/* See comment in uhci_remove_hs_ctrl() */
	if (!(sqh->qh.qh_elink & htole32(UHCI_PTR_T))) {
		sqh->qh.qh_elink = htole32(UHCI_PTR_T);
		delay(UHCI_QH_REMOVE_DELAY);
	}
	pqh = uhci_find_prev_qh(sc->sc_lctl_start, sqh);
	pqh->hlink = sqh->hlink;
	pqh->qh.qh_hlink = sqh->qh.qh_hlink;
	delay(UHCI_QH_REMOVE_DELAY);
	if (sc->sc_lctl_end == sqh)
		sc->sc_lctl_end = pqh;
}

/* Add bulk QH, called at splusb(). */
void
uhci_add_bulk(struct uhci_softc *sc, struct uhci_soft_qh *sqh)
{
	struct uhci_soft_qh *eqh;

	splsoftassert(IPL_SOFTUSB);

	DPRINTFN(10, ("uhci_add_bulk: sqh=%p\n", sqh));
	eqh = sc->sc_bulk_end;
	sqh->hlink = eqh->hlink;
	sqh->qh.qh_hlink = eqh->qh.qh_hlink;
	eqh->hlink = sqh;
	eqh->qh.qh_hlink = htole32(sqh->physaddr | UHCI_PTR_QH);
	sc->sc_bulk_end = sqh;
	uhci_add_loop(sc);
}

/* Remove bulk QH, called at splusb(). */
void
uhci_remove_bulk(struct uhci_softc *sc, struct uhci_soft_qh *sqh)
{
	struct uhci_soft_qh *pqh;

	splsoftassert(IPL_SOFTUSB);

	DPRINTFN(10, ("uhci_remove_bulk: sqh=%p\n", sqh));
	uhci_rem_loop(sc);
	/* See comment in uhci_remove_hs_ctrl() */
	if (!(sqh->qh.qh_elink & htole32(UHCI_PTR_T))) {
		sqh->qh.qh_elink = htole32(UHCI_PTR_T);
		delay(UHCI_QH_REMOVE_DELAY);
	}
	pqh = uhci_find_prev_qh(sc->sc_bulk_start, sqh);
	pqh->hlink       = sqh->hlink;
	pqh->qh.qh_hlink = sqh->qh.qh_hlink;
	delay(UHCI_QH_REMOVE_DELAY);
	if (sc->sc_bulk_end == sqh)
		sc->sc_bulk_end = pqh;
}

int uhci_intr1(struct uhci_softc *);

int
uhci_intr(void *arg)
{
	struct uhci_softc *sc = arg;

	if (sc->sc_bus.dying)
		return (0);
	if (sc->sc_bus.use_polling)
		return (0);
	return (uhci_intr1(sc));
}

int
uhci_intr1(struct uhci_softc *sc)
{
	int status;
	int ack;

	status = UREAD2(sc, UHCI_STS);
	if (status == 0xffff) {
		sc->sc_bus.dying = 1;
		return (0);
	}
	status &= UHCI_STS_ALLINTRS;
	if (status == 0)	/* The interrupt was not for us. */
		return (0);

#ifdef UHCI_DEBUG
	if (uhcidebug > 15) {
		DPRINTF(("%s: uhci_intr1\n", sc->sc_bus.bdev.dv_xname));
		uhci_dumpregs(sc);
	}
#endif

	if (sc->sc_suspend != DVACT_RESUME) {
		printf("%s: interrupt while not operating ignored\n",
		       sc->sc_bus.bdev.dv_xname);
		return (0);
	}

	ack = 0;
	if (status & UHCI_STS_USBINT)
		ack |= UHCI_STS_USBINT;
	if (status & UHCI_STS_USBEI)
		ack |= UHCI_STS_USBEI;
	if (status & UHCI_STS_RD) {
		ack |= UHCI_STS_RD;
#ifdef UHCI_DEBUG
		printf("%s: resume detect\n", sc->sc_bus.bdev.dv_xname);
#endif
	}
	if (status & UHCI_STS_HSE) {
		ack |= UHCI_STS_HSE;
		printf("%s: host system error\n", sc->sc_bus.bdev.dv_xname);
	}
	if (status & UHCI_STS_HCPE) {
		ack |= UHCI_STS_HCPE;
		printf("%s: host controller process error\n",
		       sc->sc_bus.bdev.dv_xname);
	}
	if (status & UHCI_STS_HCH) {
		/* no acknowledge needed */
		if (!sc->sc_bus.dying) {
			printf("%s: host controller halted\n",
			    sc->sc_bus.bdev.dv_xname);
#ifdef UHCI_DEBUG
			uhci_dump_all(sc);
#endif
		}
		sc->sc_bus.dying = 1;
	}

	if (!ack)
		return (0);	/* nothing to acknowledge */
	UWRITE2(sc, UHCI_STS, ack); /* acknowledge the ints */

	sc->sc_bus.no_intrs++;
	usb_schedsoftintr(&sc->sc_bus);

	DPRINTFN(15, ("%s: uhci_intr1: exit\n", sc->sc_bus.bdev.dv_xname));

	return (1);
}

void
uhci_softintr(void *v)
{
	struct uhci_softc *sc = v;
	struct uhci_xfer *ux, *nextex;

	DPRINTFN(10,("%s: uhci_softintr (%d)\n", sc->sc_bus.bdev.dv_xname,
		     sc->sc_bus.intr_context));

	if (sc->sc_bus.dying)
		return;

	sc->sc_bus.intr_context++;

	/*
	 * Interrupts on UHCI really suck.  When the host controller
	 * interrupts because a transfer is completed there is no
	 * way of knowing which transfer it was.  You can scan down
	 * the TDs and QHs of the previous frame to limit the search,
	 * but that assumes that the interrupt was not delayed by more
	 * than 1 ms, which may not always be true (e.g. after debug
	 * output on a slow console).
	 * We scan all interrupt descriptors to see if any have
	 * completed.
	 */
	for (ux = LIST_FIRST(&sc->sc_intrhead); ux; ux = nextex) {
		nextex = LIST_NEXT(ux, inext);
		uhci_check_intr(sc, &ux->xfer);
	}

	if (sc->sc_softwake) {
		sc->sc_softwake = 0;
		wakeup(&sc->sc_softwake);
	}

	sc->sc_bus.intr_context--;
}

void
uhci_check_intr(struct uhci_softc *sc, struct usbd_xfer *xfer)
{
	struct uhci_xfer *ux = (struct uhci_xfer *)xfer;
	struct uhci_soft_td *std, *lstd;
	u_int32_t status;

	DPRINTFN(15, ("%s: ux=%p\n", __func__, ux));
#ifdef DIAGNOSTIC
	if (ux == NULL) {
		printf("%s: no ux? %p\n", __func__, ux);
		return;
	}
#endif
	if (xfer->status == USBD_CANCELLED || xfer->status == USBD_TIMEOUT) {
		DPRINTF(("%s: aborted xfer=%p\n", __func__, xfer));
		return;
	}

	if (ux->stdstart == NULL)
		return;
	lstd = ux->stdend;
#ifdef DIAGNOSTIC
	if (lstd == NULL) {
		printf("%s: std==0\n", __func__);
		return;
	}
#endif
	/*
	 * If the last TD is still active we need to check whether there
	 * is an error somewhere in the middle, or whether there was a
	 * short packet (SPD and not ACTIVE).
	 */
	if (letoh32(lstd->td.td_status) & UHCI_TD_ACTIVE) {
		DPRINTFN(12, ("%s: active ux=%p\n", __func__, ux));
		for (std = ux->stdstart; std != lstd; std = std->link.std) {
			status = letoh32(std->td.td_status);
			/* If there's an active TD the xfer isn't done. */
			if (status & UHCI_TD_ACTIVE)
				break;
			/* Any kind of error makes the xfer done. */
			if (status & UHCI_TD_STALLED)
				goto done;
			/* We want short packets, and it is short: it's done */
			if ((status & UHCI_TD_SPD) &&
			      UHCI_TD_GET_ACTLEN(status) <
			      UHCI_TD_GET_MAXLEN(letoh32(std->td.td_token)))
				goto done;
		}
		DPRINTFN(12, ("%s: ux=%p std=%p still active\n", __func__,
			      ux, ux->stdstart));
		return;
	}
 done:
	DPRINTFN(12, ("uhci_check_intr: ux=%p done\n", ux));
	timeout_del(&xfer->timeout_handle);
	usb_rem_task(xfer->pipe->device, &xfer->abort_task);
	uhci_idone(xfer);
}

/* Called at splusb() */
void
uhci_idone(struct usbd_xfer *xfer)
{
	struct uhci_xfer *ux = (struct uhci_xfer *)xfer;
	struct uhci_pipe *upipe = (struct uhci_pipe *)xfer->pipe;
	struct uhci_soft_td *std;
	u_int32_t status = 0, nstatus;
	int actlen;

	DPRINTFN(12, ("uhci_idone: ux=%p\n", ux));
#ifdef DIAGNOSTIC
	{
		int s = splhigh();
		if (ux->isdone) {
			splx(s);
#ifdef UHCI_DEBUG
			printf("uhci_idone: ux is done!\n   ");
			uhci_dump_xfer(ux);
#else
			printf("uhci_idone: ux=%p is done!\n", ux);
#endif
			return;
		}
		ux->isdone = 1;
		splx(s);
	}
#endif

	if (xfer->nframes != 0) {
		/* Isoc transfer, do things differently. */
		struct uhci_soft_td **stds = upipe->u.iso.stds;
		int i, n, nframes, len;

		DPRINTFN(5,("uhci_idone: ux=%p isoc ready\n", ux));

		nframes = xfer->nframes;
		actlen = 0;
		n = ux->curframe;
		for (i = 0; i < nframes; i++) {
			std = stds[n];
#ifdef UHCI_DEBUG
			if (uhcidebug > 5) {
				DPRINTFN(-1,("uhci_idone: isoc TD %d\n", i));
				uhci_dump_td(std);
			}
#endif
			if (++n >= UHCI_VFRAMELIST_COUNT)
				n = 0;
			status = letoh32(std->td.td_status);
			len = UHCI_TD_GET_ACTLEN(status);
			xfer->frlengths[i] = len;
			actlen += len;
		}
		upipe->u.iso.inuse -= nframes;
		usb_syncmem(&xfer->dmabuf, 0, xfer->length,
		    usbd_xfer_isread(xfer) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		xfer->actlen = actlen;
		xfer->status = USBD_NORMAL_COMPLETION;
		goto end;
	}

#ifdef UHCI_DEBUG
	DPRINTFN(10, ("uhci_idone: ux=%p, xfer=%p, pipe=%p ready\n",
		      ux, xfer, upipe));
	if (uhcidebug > 10)
		uhci_dump_tds(ux->stdstart);
#endif

	/* The transfer is done, compute actual length and status. */
	actlen = 0;
	for (std = ux->stdstart; std != NULL; std = std->link.std) {
		nstatus = letoh32(std->td.td_status);
		if (nstatus & UHCI_TD_ACTIVE)
			break;

		status = nstatus;
		if (UHCI_TD_GET_PID(letoh32(std->td.td_token)) !=
		    UHCI_TD_PID_SETUP)
			actlen += UHCI_TD_GET_ACTLEN(status);
		else {
			/*
			 * UHCI will report CRCTO in addition to a STALL or NAK
			 * for a SETUP transaction.  See section 3.2.2, "TD
			 * CONTROL AND STATUS".
			 */
			if (status & (UHCI_TD_STALLED | UHCI_TD_NAK))
				status &= ~UHCI_TD_CRCTO;
		}
	}
	/* If there are left over TDs we need to update the toggle. */
	if (std != NULL)
		upipe->nexttoggle = UHCI_TD_GET_DT(letoh32(std->td.td_token));

	status &= UHCI_TD_ERROR;
	DPRINTFN(10, ("uhci_idone: actlen=%d, status=0x%x\n",
		      actlen, status));
	xfer->actlen = actlen;
	if (status != 0) {
#ifdef UHCI_DEBUG
		char sbuf[128];

		bitmask_snprintf((u_int32_t)status,
				 "\20\22BITSTUFF\23CRCTO\24NAK\25"
				 "BABBLE\26DBUFFER\27STALLED\30ACTIVE",
				 sbuf, sizeof(sbuf));

		DPRINTFN((status == UHCI_TD_STALLED)*10,
			 ("uhci_idone: error, addr=%d, endpt=0x%02x, "
			  "status 0x%s\n",
			  xfer->device->address,
			  xfer->pipe->endpoint->edesc->bEndpointAddress,
			  sbuf));
#endif

		if (status == UHCI_TD_STALLED)
			xfer->status = USBD_STALLED;
		else
			xfer->status = USBD_IOERROR; /* more info XXX */
	} else {
		if (xfer->actlen)
			usb_syncmem(&xfer->dmabuf, 0, xfer->actlen,
			    usbd_xfer_isread(xfer) ?
			    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		xfer->status = USBD_NORMAL_COMPLETION;
	}

 end:
	usb_transfer_complete(xfer);
	DPRINTFN(12, ("uhci_idone: ux=%p done\n", ux));
}

void
uhci_timeout(void *addr)
{
	struct usbd_xfer *xfer = addr;
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;

	if (sc->sc_bus.dying) {
		uhci_timeout_task(addr);
		return;
	}

	usb_init_task(&xfer->abort_task, uhci_timeout_task, addr,
	    USB_TASK_TYPE_ABORT);
	usb_add_task(xfer->device, &xfer->abort_task);
}

void
uhci_timeout_task(void *addr)
{
	struct usbd_xfer *xfer = addr;
	int s;

	DPRINTF(("%s: xfer=%p\n", __func__, xfer));

	s = splusb();
	uhci_abort_xfer(xfer, USBD_TIMEOUT);
	splx(s);
}

void
uhci_poll(struct usbd_bus *bus)
{
	struct uhci_softc *sc = (struct uhci_softc *)bus;

	if (UREAD2(sc, UHCI_STS) & UHCI_STS_ALLINTRS)
		uhci_intr1(sc);
}

void
uhci_reset(struct uhci_softc *sc)
{
	int n;

	UHCICMD(sc, UHCI_CMD_HCRESET);
	/* The reset bit goes low when the controller is done. */
	for (n = 0; n < UHCI_RESET_TIMEOUT &&
		    (UREAD2(sc, UHCI_CMD) & UHCI_CMD_HCRESET); n++)
		usb_delay_ms(&sc->sc_bus, 1);
	if (n >= UHCI_RESET_TIMEOUT)
		printf("%s: controller did not reset\n",
		       sc->sc_bus.bdev.dv_xname);
}

usbd_status
uhci_run(struct uhci_softc *sc, int run)
{
	int s, n, running;
	u_int16_t cmd;

	run = run != 0;
	s = splhardusb();
	DPRINTF(("uhci_run: setting run=%d\n", run));
	cmd = UREAD2(sc, UHCI_CMD);
	if (run)
		cmd |= UHCI_CMD_RS;
	else
		cmd &= ~UHCI_CMD_RS;
	UHCICMD(sc, cmd);
	for(n = 0; n < 10; n++) {
		running = !(UREAD2(sc, UHCI_STS) & UHCI_STS_HCH);
		/* return when we've entered the state we want */
		if (run == running) {
			splx(s);
			DPRINTF(("uhci_run: done cmd=0x%x sts=0x%x\n",
				 UREAD2(sc, UHCI_CMD), UREAD2(sc, UHCI_STS)));
			return (USBD_NORMAL_COMPLETION);
		}
		usb_delay_ms(&sc->sc_bus, 1);
	}
	splx(s);
	printf("%s: cannot %s\n", sc->sc_bus.bdev.dv_xname,
	       run ? "start" : "stop");
	return (USBD_IOERROR);
}

/*
 * Memory management routines.
 *  uhci_alloc_std allocates TDs
 *  uhci_alloc_sqh allocates QHs
 * These two routines do their own free list management,
 * partly for speed, partly because allocating DMAable memory
 * has page size granularity so much memory would be wasted if
 * only one TD/QH (32 bytes) was placed in each allocated chunk.
 */

struct uhci_soft_td *
uhci_alloc_std(struct uhci_softc *sc)
{
	struct uhci_soft_td *std = NULL;
	usbd_status err;
	int i, offs;
	struct usb_dma dma;
	int s;

	s = splusb();
	if (sc->sc_freetds == NULL) {
		DPRINTFN(2,("uhci_alloc_std: allocating chunk\n"));
		err = usb_allocmem(&sc->sc_bus, UHCI_STD_SIZE * UHCI_STD_CHUNK,
			  UHCI_TD_ALIGN, USB_DMA_COHERENT, &dma);
		if (err)
			goto out;
		for(i = 0; i < UHCI_STD_CHUNK; i++) {
			offs = i * UHCI_STD_SIZE;
			std = KERNADDR(&dma, offs);
			std->physaddr = DMAADDR(&dma, offs);
			std->link.std = sc->sc_freetds;
			sc->sc_freetds = std;
		}
	}

	std = sc->sc_freetds;
	sc->sc_freetds = std->link.std;
	memset(&std->td, 0, sizeof(struct uhci_td));

out:
	splx(s);
	return (std);
}

void
uhci_free_std(struct uhci_softc *sc, struct uhci_soft_td *std)
{
	int s;

#ifdef DIAGNOSTIC
#define TD_IS_FREE 0x12345678
	if (letoh32(std->td.td_token) == TD_IS_FREE) {
		printf("uhci_free_std: freeing free TD %p\n", std);
		return;
	}
	std->td.td_token = htole32(TD_IS_FREE);
#endif

	s = splusb();
	std->link.std = sc->sc_freetds;
	sc->sc_freetds = std;
	splx(s);
}

struct uhci_soft_qh *
uhci_alloc_sqh(struct uhci_softc *sc)
{
	struct uhci_soft_qh *sqh = NULL;
	usbd_status err;
	int i, offs;
	struct usb_dma dma;
	int s;

	s = splusb();
	if (sc->sc_freeqhs == NULL) {
		DPRINTFN(2, ("uhci_alloc_sqh: allocating chunk\n"));
		err = usb_allocmem(&sc->sc_bus, UHCI_SQH_SIZE * UHCI_SQH_CHUNK,
			  UHCI_QH_ALIGN, USB_DMA_COHERENT, &dma);
		if (err)
			goto out;
		for (i = 0; i < UHCI_SQH_CHUNK; i++) {
			offs = i * UHCI_SQH_SIZE;
			sqh = KERNADDR(&dma, offs);
			sqh->physaddr = DMAADDR(&dma, offs);
			sqh->hlink = sc->sc_freeqhs;
			sc->sc_freeqhs = sqh;
		}
	}
	sqh = sc->sc_freeqhs;
	sc->sc_freeqhs = sqh->hlink;
	memset(&sqh->qh, 0, sizeof(struct uhci_qh));

out:
	splx(s);
	return (sqh);
}

void
uhci_free_sqh(struct uhci_softc *sc, struct uhci_soft_qh *sqh)
{
	sqh->hlink = sc->sc_freeqhs;
	sc->sc_freeqhs = sqh;
}

void
uhci_free_std_chain(struct uhci_softc *sc, struct uhci_soft_td *std,
    struct uhci_soft_td *stdend)
{
	struct uhci_soft_td *p;

	for (; std != stdend; std = p) {
		p = std->link.std;
		uhci_free_std(sc, std);
	}
}

usbd_status
uhci_alloc_std_chain(struct uhci_softc *sc, u_int len, struct usbd_xfer *xfer,
    struct uhci_soft_td **sp, struct uhci_soft_td **ep)
{
	struct uhci_pipe *upipe = (struct uhci_pipe *)xfer->pipe;
	struct uhci_soft_td *p, *lastp;
	uhci_physaddr_t lastlink;
	int i, ntd, l, tog, mps;
	u_int32_t status;
	u_int16_t flags = xfer->flags;
	int rd = usbd_xfer_isread(xfer);
	struct usb_dma *dma = &xfer->dmabuf;
	int addr = xfer->device->address;
	int endpt = xfer->pipe->endpoint->edesc->bEndpointAddress;

	DPRINTFN(8, ("%s: addr=%d endpt=%d len=%u speed=%d flags=0x%x\n",
	    __func__, addr, UE_GET_ADDR(endpt), len, xfer->device->speed,
	    flags));

	usb_syncmem(&xfer->dmabuf, 0, xfer->length,
	    usbd_xfer_isread(xfer) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	mps = UGETW(xfer->pipe->endpoint->edesc->wMaxPacketSize);
	if (mps == 0) {
		printf("uhci_alloc_std_chain: mps=0\n");
		return (USBD_INVAL);
	}
	ntd = (len + mps - 1) / mps;
	if (len == 0)
		flags |= USBD_FORCE_SHORT_XFER;
	if ((flags & USBD_FORCE_SHORT_XFER) && len % mps == 0)
		ntd++;
	DPRINTFN(10, ("%s: mps=%d ntd=%d\n", __func__, mps, ntd));
	tog = upipe->nexttoggle;
	if (ntd % 2 == 0)
		tog ^= 1;
	upipe->nexttoggle = tog ^ 1;
	lastp = NULL;
	lastlink = UHCI_PTR_T;
	ntd--;
	status = UHCI_TD_ZERO_ACTLEN(UHCI_TD_SET_ERRCNT(3) | UHCI_TD_ACTIVE);
	if (xfer->pipe->device->speed == USB_SPEED_LOW)
		status |= UHCI_TD_LS;
	if (flags & USBD_SHORT_XFER_OK)
		status |= UHCI_TD_SPD;
	for (i = ntd; i >= 0; i--) {
		p = uhci_alloc_std(sc);
		if (p == NULL) {
			uhci_free_std_chain(sc, lastp, NULL);
			return (USBD_NOMEM);
		}
		p->link.std = lastp;
		p->td.td_link = htole32(lastlink | UHCI_PTR_VF | UHCI_PTR_TD);
		lastp = p;
		lastlink = p->physaddr;
		p->td.td_status = htole32(status);
		if (i == ntd) {
			/* last TD */
			l = len % mps;
			if (l == 0 && !(flags & USBD_FORCE_SHORT_XFER))
				l = mps;
			*ep = p;
		} else
			l = mps;
		p->td.td_token =
		    htole32(rd ? UHCI_TD_IN (l, endpt, addr, tog) :
				 UHCI_TD_OUT(l, endpt, addr, tog));
		p->td.td_buffer = htole32(DMAADDR(dma, i * mps));
		tog ^= 1;
	}
	*sp = lastp;
	DPRINTFN(10, ("%s: nexttog=%d\n", __func__, upipe->nexttoggle));
	return (USBD_NORMAL_COMPLETION);
}

void
uhci_device_clear_toggle(struct usbd_pipe *pipe)
{
	struct uhci_pipe *upipe = (struct uhci_pipe *)pipe;
	upipe->nexttoggle = 0;
}

usbd_status
uhci_device_bulk_transfer(struct usbd_xfer *xfer)
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
	return (uhci_device_bulk_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
uhci_device_bulk_start(struct usbd_xfer *xfer)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;
	struct uhci_pipe *upipe = (struct uhci_pipe *)xfer->pipe;
	struct uhci_xfer *ux = (struct uhci_xfer *)xfer;
	struct uhci_soft_td *data, *dataend;
	struct uhci_soft_qh *sqh;
	usbd_status err;
	u_int len;
	int s;

	DPRINTFN(3, ("uhci_device_bulk_start: xfer=%p len=%u flags=%d ux=%p\n",
		     xfer, xfer->length, xfer->flags, ux));

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

#ifdef DIAGNOSTIC
	if (xfer->rqflags & URQ_REQUEST)
		panic("uhci_device_bulk_start: a request");
#endif

	len = xfer->length;
	sqh = upipe->u.bulk.sqh;

	err = uhci_alloc_std_chain(sc, len, xfer, &data, &dataend);
	if (err)
		return (err);
	dataend->td.td_status |= htole32(UHCI_TD_IOC);

#ifdef UHCI_DEBUG
	if (uhcidebug > 8) {
		DPRINTF(("uhci_device_bulk_start: data(1)\n"));
		uhci_dump_tds(data);
	}
#endif

	/* Set up interrupt info. */
	ux->stdstart = data;
	ux->stdend = dataend;
#ifdef DIAGNOSTIC
	if (!ux->isdone) {
		printf("uhci_device_bulk_start: not done, ux=%p\n", ux);
	}
	ux->isdone = 0;
#endif

	sqh->elink = data;
	sqh->qh.qh_elink = htole32(data->physaddr | UHCI_PTR_TD);

	s = splusb();
	uhci_add_bulk(sc, sqh);
	uhci_add_intr_list(sc, ux);

	if (xfer->timeout && !sc->sc_bus.use_polling) {
		timeout_del(&xfer->timeout_handle);
		timeout_set(&xfer->timeout_handle, uhci_timeout, xfer);
		timeout_add_msec(&xfer->timeout_handle, xfer->timeout);
	}
	xfer->status = USBD_IN_PROGRESS;
	splx(s);

#ifdef UHCI_DEBUG
	if (uhcidebug > 10) {
		DPRINTF(("uhci_device_bulk_start: data(2)\n"));
		uhci_dump_tds(data);
	}
#endif

	return (USBD_IN_PROGRESS);
}

/* Abort a device bulk request. */
void
uhci_device_bulk_abort(struct usbd_xfer *xfer)
{
	DPRINTF(("uhci_device_bulk_abort:\n"));
	uhci_abort_xfer(xfer, USBD_CANCELLED);
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
uhci_abort_xfer(struct usbd_xfer *xfer, usbd_status status)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;
	struct uhci_xfer *ux = (struct uhci_xfer *)xfer;
	struct uhci_soft_td *std;
	int s;

	DPRINTFN(1,("uhci_abort_xfer: xfer=%p, status=%d\n", xfer, status));

	if (sc->sc_bus.dying) {
		/* If we're dying, just do the software part. */
		s = splusb();
		xfer->status = status;	/* make software ignore it */
		timeout_del(&xfer->timeout_handle);
		usb_rem_task(xfer->device, &xfer->abort_task);
#ifdef DIAGNOSTIC
		ux->isdone = 1;
#endif
		usb_transfer_complete(xfer);
		splx(s);
		return;
	}

	if (xfer->device->bus->intr_context || !curproc)
		panic("uhci_abort_xfer: not in process context");

	/*
	 * Step 1: Make interrupt routine and hardware ignore xfer.
	 */
	s = splusb();
	xfer->status = status;	/* make software ignore it */
	timeout_del(&xfer->timeout_handle);
	usb_rem_task(xfer->device, &xfer->abort_task);
	DPRINTFN(1,("uhci_abort_xfer: stop ux=%p\n", ux));
	for (std = ux->stdstart; std != NULL; std = std->link.std)
		std->td.td_status &= htole32(~(UHCI_TD_ACTIVE | UHCI_TD_IOC));
	splx(s);

	/*
	 * Step 2: Wait until we know hardware has finished any possible
	 * use of the xfer.  Also make sure the soft interrupt routine
	 * has run.
	 */
	usb_delay_ms(&sc->sc_bus, 2); /* Hardware finishes in 1ms */
	s = splusb();
	sc->sc_softwake = 1;
	usb_schedsoftintr(&sc->sc_bus);
	DPRINTFN(1,("uhci_abort_xfer: tsleep\n"));
	tsleep_nsec(&sc->sc_softwake, PZERO, "uhciab", INFSLP);
	splx(s);

	/*
	 * Step 3: Execute callback.
	 */
	DPRINTFN(1,("uhci_abort_xfer: callback\n"));
	s = splusb();
#ifdef DIAGNOSTIC
	ux->isdone = 1;
#endif
	usb_transfer_complete(xfer);
	splx(s);
}

/* Close a device bulk pipe. */
void
uhci_device_bulk_close(struct usbd_pipe *pipe)
{
	struct uhci_softc *sc = (struct uhci_softc *)pipe->device->bus;
	struct uhci_pipe *upipe = (struct uhci_pipe *)pipe;

	uhci_free_sqh(sc, upipe->u.bulk.sqh);
	pipe->endpoint->savedtoggle = upipe->nexttoggle;
}

usbd_status
uhci_device_ctrl_transfer(struct usbd_xfer *xfer)
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
	return (uhci_device_ctrl_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
uhci_device_ctrl_start(struct usbd_xfer *xfer)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;
	usbd_status err;

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

#ifdef DIAGNOSTIC
	if (!(xfer->rqflags & URQ_REQUEST))
		panic("uhci_device_ctrl_transfer: not a request");
#endif

	err = uhci_device_request(xfer);
	if (err)
		return (err);

	return (USBD_IN_PROGRESS);
}

usbd_status
uhci_device_intr_transfer(struct usbd_xfer *xfer)
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
	return (uhci_device_intr_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
uhci_device_intr_start(struct usbd_xfer *xfer)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;
	struct uhci_pipe *upipe = (struct uhci_pipe *)xfer->pipe;
	struct uhci_xfer *ux = (struct uhci_xfer *)xfer;
	struct uhci_soft_td *data, *dataend;
	struct uhci_soft_qh *sqh;
	usbd_status err;
	int i, s;

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	DPRINTFN(3,("uhci_device_intr_start: xfer=%p len=%u flags=%d\n",
		    xfer, xfer->length, xfer->flags));

#ifdef DIAGNOSTIC
	if (xfer->rqflags & URQ_REQUEST)
		panic("uhci_device_intr_start: a request");
#endif

	upipe->u.intr.isread = usbd_xfer_isread(xfer);

	err = uhci_alloc_std_chain(sc, xfer->length, xfer, &data, &dataend);

	if (err)
		return (err);
	dataend->td.td_status |= htole32(UHCI_TD_IOC);

#ifdef UHCI_DEBUG
	if (uhcidebug > 10) {
		DPRINTF(("uhci_device_intr_start: data(1)\n"));
		uhci_dump_tds(data);
		uhci_dump_qh(upipe->u.intr.qhs[0]);
	}
#endif

	s = splusb();
	/* Set up interrupt info. */
	ux->stdstart = data;
	ux->stdend = dataend;
#ifdef DIAGNOSTIC
	if (!ux->isdone) {
		printf("uhci_device_intr_transfer: not done, ux=%p\n", ux);
	}
	ux->isdone = 0;
#endif

	DPRINTFN(10,("uhci_device_intr_start: qhs[0]=%p\n",
		     upipe->u.intr.qhs[0]));
	for (i = 0; i < upipe->u.intr.npoll; i++) {
		sqh = upipe->u.intr.qhs[i];
		sqh->elink = data;
		sqh->qh.qh_elink = htole32(data->physaddr | UHCI_PTR_TD);
	}
	uhci_add_intr_list(sc, ux);
	xfer->status = USBD_IN_PROGRESS;
	splx(s);

#ifdef UHCI_DEBUG
	if (uhcidebug > 10) {
		DPRINTF(("uhci_device_intr_start: data(2)\n"));
		uhci_dump_tds(data);
		uhci_dump_qh(upipe->u.intr.qhs[0]);
	}
#endif

	return (USBD_IN_PROGRESS);
}

/* Abort a device control request. */
void
uhci_device_ctrl_abort(struct usbd_xfer *xfer)
{
	DPRINTF(("uhci_device_ctrl_abort:\n"));
	uhci_abort_xfer(xfer, USBD_CANCELLED);
}

/* Close a device control pipe. */
void
uhci_device_ctrl_close(struct usbd_pipe *pipe)
{
}

void
uhci_device_intr_abort(struct usbd_xfer *xfer)
{
	KASSERT(!xfer->pipe->repeat || xfer->pipe->intrxfer == xfer);

	uhci_abort_xfer(xfer, USBD_CANCELLED);
}

/* Close a device interrupt pipe. */
void
uhci_device_intr_close(struct usbd_pipe *pipe)
{
	struct uhci_pipe *upipe = (struct uhci_pipe *)pipe;
	struct uhci_softc *sc = (struct uhci_softc *)pipe->device->bus;
	struct uhci_soft_qh **qhs;
	int i, npoll;
	int s;

	/* Unlink descriptors from controller data structures. */
	qhs = upipe->u.intr.qhs;
	npoll = upipe->u.intr.npoll;
	s = splusb();
	for (i = 0; i < npoll; i++)
		uhci_remove_intr(sc, upipe->u.intr.qhs[i]);
	splx(s);

	/*
	 * We now have to wait for any activity on the physical
	 * descriptors to stop.
	 */
	usb_delay_ms(&sc->sc_bus, 2);

	for(i = 0; i < npoll; i++)
		uhci_free_sqh(sc, upipe->u.intr.qhs[i]);
	free(qhs, M_USBHC, npoll * sizeof(*qhs));

	/* XXX free other resources */
}

usbd_status
uhci_device_request(struct usbd_xfer *xfer)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;
	struct uhci_pipe *upipe = (struct uhci_pipe *)xfer->pipe;
	struct uhci_xfer *ux = (struct uhci_xfer *)xfer;
	usb_device_request_t *req = &xfer->request;
	int addr = xfer->device->address;
	int endpt = xfer->pipe->endpoint->edesc->bEndpointAddress;
	struct uhci_soft_td *setup, *data, *stat, *next, *dataend;
	struct uhci_soft_qh *sqh;
	u_int len;
	u_int32_t ls;
	usbd_status err;
	int s;

	DPRINTFN(3,("uhci_device_request type=0x%02x, request=0x%02x, "
		    "wValue=0x%04x, wIndex=0x%04x len=%u, addr=%d, endpt=%d\n",
		    req->bmRequestType, req->bRequest, UGETW(req->wValue),
		    UGETW(req->wIndex), UGETW(req->wLength),
		    addr, endpt));

	ls = xfer->device->speed == USB_SPEED_LOW ? UHCI_TD_LS : 0;
	len = UGETW(req->wLength);

	setup = upipe->u.ctl.setup;
	stat = upipe->u.ctl.stat;
	sqh = upipe->u.ctl.sqh;

	/* Set up data transaction */
	if (len != 0) {
		upipe->nexttoggle = 1;
		err = uhci_alloc_std_chain(sc, len, xfer, &data, &dataend);
		if (err)
			return (err);
		next = data;
		dataend->link.std = stat;
		dataend->td.td_link = htole32(stat->physaddr | UHCI_PTR_VF | UHCI_PTR_TD);
	} else {
		next = stat;
	}
	upipe->u.ctl.length = len;

	memcpy(KERNADDR(&upipe->u.ctl.reqdma, 0), req, sizeof *req);

	setup->link.std = next;
	setup->td.td_link = htole32(next->physaddr | UHCI_PTR_VF | UHCI_PTR_TD);
	setup->td.td_status = htole32(UHCI_TD_SET_ERRCNT(3) | ls |
		UHCI_TD_ACTIVE);
	setup->td.td_token = htole32(UHCI_TD_SETUP(sizeof *req, endpt, addr));
	setup->td.td_buffer = htole32(DMAADDR(&upipe->u.ctl.reqdma, 0));

	stat->link.std = NULL;
	stat->td.td_link = htole32(UHCI_PTR_T);
	stat->td.td_status = htole32(UHCI_TD_SET_ERRCNT(3) | ls |
		UHCI_TD_ACTIVE | UHCI_TD_IOC);
	stat->td.td_token = htole32(usbd_xfer_isread(xfer) ?
	    UHCI_TD_OUT(0, endpt, addr, 1) : UHCI_TD_IN (0, endpt, addr, 1));
	stat->td.td_buffer = htole32(0);

#ifdef UHCI_DEBUG
	if (uhcidebug > 10) {
		DPRINTF(("uhci_device_request: before transfer\n"));
		uhci_dump_tds(setup);
	}
#endif

	/* Set up interrupt info. */
	ux->stdstart = setup;
	ux->stdend = stat;
#ifdef DIAGNOSTIC
	if (!ux->isdone) {
		printf("%s: not done, ux=%p\n", __func__, ux);
	}
	ux->isdone = 0;
#endif

	sqh->elink = setup;
	sqh->qh.qh_elink = htole32(setup->physaddr | UHCI_PTR_TD);

	s = splusb();
	if (xfer->device->speed == USB_SPEED_LOW)
		uhci_add_ls_ctrl(sc, sqh);
	else
		uhci_add_hs_ctrl(sc, sqh);
	uhci_add_intr_list(sc, ux);
#ifdef UHCI_DEBUG
	if (uhcidebug > 12) {
		struct uhci_soft_td *std;
		struct uhci_soft_qh *xqh;
		struct uhci_soft_qh *sxqh;
		int maxqh = 0;
		uhci_physaddr_t link;
		DPRINTF(("uhci_device_request: follow from [0]\n"));
		for (std = sc->sc_vframes[0].htd, link = 0;
		     (link & UHCI_PTR_QH) == 0;
		     std = std->link.std) {
			link = letoh32(std->td.td_link);
			uhci_dump_td(std);
		}
		sxqh = (struct uhci_soft_qh *)std;
		uhci_dump_qh(sxqh);
		for (xqh = sxqh;
		     xqh != NULL;
		     xqh = (maxqh++ == 5 || xqh->hlink == sxqh ||
                            xqh->hlink == xqh ? NULL : xqh->hlink)) {
			uhci_dump_qh(xqh);
		}
		DPRINTF(("Enqueued QH:\n"));
		uhci_dump_qh(sqh);
		uhci_dump_tds(sqh->elink);
	}
#endif
	if (xfer->timeout && !sc->sc_bus.use_polling) {
		timeout_del(&xfer->timeout_handle);
		timeout_set(&xfer->timeout_handle, uhci_timeout, xfer);
		timeout_add_msec(&xfer->timeout_handle, xfer->timeout);
	}
	xfer->status = USBD_IN_PROGRESS;
	splx(s);

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
uhci_device_isoc_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	DPRINTFN(5,("uhci_device_isoc_transfer: xfer=%p\n", xfer));

	/* Put it on our queue, */
	err = usb_insert_transfer(xfer);

	/* bail out on error, */
	if (err && err != USBD_IN_PROGRESS)
		return (err);

	/* XXX should check inuse here */

	/* insert into schedule, */
	uhci_device_isoc_enter(xfer);

	/* and start if the pipe wasn't running */
	if (!err)
		uhci_device_isoc_start(SIMPLEQ_FIRST(&xfer->pipe->queue));

	return (err);
}

void
uhci_device_isoc_enter(struct usbd_xfer *xfer)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;
	struct uhci_pipe *upipe = (struct uhci_pipe *)xfer->pipe;
	struct iso *iso = &upipe->u.iso;
	struct uhci_soft_td *std;
	u_int32_t buf, len, status;
	int s, i, next, nframes;

	DPRINTFN(5,("uhci_device_isoc_enter: used=%d next=%d xfer=%p "
		    "nframes=%d\n",
		    iso->inuse, iso->next, xfer, xfer->nframes));

	if (sc->sc_bus.dying)
		return;

	if (xfer->status == USBD_IN_PROGRESS) {
		/* This request has already been entered into the frame list */
		printf("uhci_device_isoc_enter: xfer=%p in frame list\n", xfer);
		/* XXX */
	}

#ifdef DIAGNOSTIC
	if (iso->inuse >= UHCI_VFRAMELIST_COUNT)
		printf("uhci_device_isoc_enter: overflow!\n");
#endif

	usb_syncmem(&xfer->dmabuf, 0, xfer->length,
	    usbd_xfer_isread(xfer) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	next = iso->next;
	if (next == -1) {
		/* Not in use yet, schedule it a few frames ahead. */
		next = (UREAD2(sc, UHCI_FRNUM) + 3) % UHCI_VFRAMELIST_COUNT;
		DPRINTFN(2,("uhci_device_isoc_enter: start next=%d\n", next));
	}

	xfer->status = USBD_IN_PROGRESS;
	((struct uhci_xfer *)xfer)->curframe = next;

	buf = DMAADDR(&xfer->dmabuf, 0);
	status = UHCI_TD_ZERO_ACTLEN(UHCI_TD_SET_ERRCNT(0) |
				     UHCI_TD_ACTIVE |
				     UHCI_TD_IOS);
	nframes = xfer->nframes;
	s = splusb();
	for (i = 0; i < nframes; i++) {
		std = iso->stds[next];
		if (++next >= UHCI_VFRAMELIST_COUNT)
			next = 0;
		len = xfer->frlengths[i];
		std->td.td_buffer = htole32(buf);
		if (i == nframes - 1)
			status |= UHCI_TD_IOC;
		std->td.td_status = htole32(status);
		std->td.td_token &= htole32(~UHCI_TD_MAXLEN_MASK);
		std->td.td_token |= htole32(UHCI_TD_SET_MAXLEN(len));
#ifdef UHCI_DEBUG
		if (uhcidebug > 5) {
			DPRINTFN(5,("uhci_device_isoc_enter: TD %d\n", i));
			uhci_dump_td(std);
		}
#endif
		buf += len;
	}
	iso->next = next;
	iso->inuse += xfer->nframes;

	splx(s);
}

usbd_status
uhci_device_isoc_start(struct usbd_xfer *xfer)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;
	struct uhci_pipe *upipe = (struct uhci_pipe *)xfer->pipe;
	struct uhci_xfer *ux = (struct uhci_xfer *)xfer;
	struct uhci_soft_td *end;
	int s, i;

	DPRINTFN(5,("uhci_device_isoc_start: xfer=%p\n", xfer));

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

#ifdef DIAGNOSTIC
	if (xfer->status != USBD_IN_PROGRESS)
		printf("uhci_device_isoc_start: not in progress %p\n", xfer);
#endif

	/* Find the last TD */
	i = ux->curframe + (xfer->nframes - 1);
	if (i >= UHCI_VFRAMELIST_COUNT)
		i -= UHCI_VFRAMELIST_COUNT;
	end = upipe->u.iso.stds[i];

#ifdef DIAGNOSTIC
	if (end == NULL) {
		printf("uhci_device_isoc_start: end == NULL\n");
		return (USBD_INVAL);
	}
#endif

	s = splusb();

	/* Set up interrupt info. */
	ux->stdstart = end;
	ux->stdend = end;
#ifdef DIAGNOSTIC
	if (!ux->isdone)
		printf("%s: not done, ux=%p\n", __func__, ux);
	ux->isdone = 0;
#endif
	uhci_add_intr_list(sc, ux);

	splx(s);

	return (USBD_IN_PROGRESS);
}

void
uhci_device_isoc_abort(struct usbd_xfer *xfer)
{
	struct uhci_xfer *ux = (struct uhci_xfer *)xfer;
	struct uhci_pipe *upipe = (struct uhci_pipe *)xfer->pipe;
	struct uhci_soft_td **stds = upipe->u.iso.stds;
	struct uhci_soft_td *std;
	int i, n, s, nframes, maxlen, len;

	s = splusb();

	/* Transfer is already done. */
	if (xfer->status != USBD_NOT_STARTED &&
	    xfer->status != USBD_IN_PROGRESS) {
		splx(s);
		return;
	}

	/* Give xfer the requested abort code. */
	xfer->status = USBD_CANCELLED;

	/* make hardware ignore it, */
	nframes = xfer->nframes;
	n = ux->curframe;
	maxlen = 0;
	for (i = 0; i < nframes; i++) {
		std = stds[n];
		std->td.td_status &= htole32(~(UHCI_TD_ACTIVE | UHCI_TD_IOC));
		len = UHCI_TD_GET_MAXLEN(letoh32(std->td.td_token));
		if (len > maxlen)
			maxlen = len;
		if (++n >= UHCI_VFRAMELIST_COUNT)
			n = 0;
	}

	/* and wait until we are sure the hardware has finished. */
	delay(maxlen);

#ifdef DIAGNOSTIC
	ux->isdone = 1;
#endif
	/* Run callback and remove from interrupt list. */
	usb_transfer_complete(xfer);

	splx(s);
}

void
uhci_device_isoc_close(struct usbd_pipe *pipe)
{
	struct uhci_softc *sc = (struct uhci_softc *)pipe->device->bus;
	struct uhci_pipe *upipe = (struct uhci_pipe *)pipe;
	struct uhci_soft_td *std, *vstd;
	struct iso *iso;
	int i, s;

	/*
	 * Make sure all TDs are marked as inactive.
	 * Wait for completion.
	 * Unschedule.
	 * Deallocate.
	 */
	iso = &upipe->u.iso;

	for (i = 0; i < UHCI_VFRAMELIST_COUNT; i++)
		iso->stds[i]->td.td_status &= htole32(~UHCI_TD_ACTIVE);
	usb_delay_ms(&sc->sc_bus, 2); /* wait for completion */

	s = splusb();
	for (i = 0; i < UHCI_VFRAMELIST_COUNT; i++) {
		std = iso->stds[i];
		for (vstd = sc->sc_vframes[i].htd;
		     vstd != NULL && vstd->link.std != std;
		     vstd = vstd->link.std)
			;
		if (vstd == NULL) {
			/*panic*/
			printf("uhci_device_isoc_close: %p not found\n", std);
			splx(s);
			return;
		}
		vstd->link = std->link;
		vstd->td.td_link = std->td.td_link;
		uhci_free_std(sc, std);
	}
	splx(s);

	free(iso->stds, M_USBHC, UHCI_VFRAMELIST_COUNT * sizeof(*iso->stds));
}

usbd_status
uhci_setup_isoc(struct usbd_pipe *pipe)
{
	struct uhci_softc *sc = (struct uhci_softc *)pipe->device->bus;
	struct uhci_pipe *upipe = (struct uhci_pipe *)pipe;
	int addr = pipe->device->address;
	int endpt = pipe->endpoint->edesc->bEndpointAddress;
	int rd = UE_GET_DIR(endpt) == UE_DIR_IN;
	struct uhci_soft_td *std, *vstd;
	u_int32_t token;
	struct iso *iso;
	int i, s;

	iso = &upipe->u.iso;
	iso->stds = mallocarray(UHCI_VFRAMELIST_COUNT, sizeof(*iso->stds),
	    M_USBHC, M_WAITOK);

	token = rd ? UHCI_TD_IN (0, endpt, addr, 0) :
		     UHCI_TD_OUT(0, endpt, addr, 0);

	/* Allocate the TDs and mark as inactive; */
	for (i = 0; i < UHCI_VFRAMELIST_COUNT; i++) {
		std = uhci_alloc_std(sc);
		if (std == 0)
			goto bad;
		std->td.td_status = htole32(UHCI_TD_IOS); /* iso, inactive */
		std->td.td_token = htole32(token);
		iso->stds[i] = std;
	}

	/* Insert TDs into schedule. */
	s = splusb();
	for (i = 0; i < UHCI_VFRAMELIST_COUNT; i++) {
		std = iso->stds[i];
		vstd = sc->sc_vframes[i].htd;
		std->link = vstd->link;
		std->td.td_link = vstd->td.td_link;
		vstd->link.std = std;
		vstd->td.td_link = htole32(std->physaddr | UHCI_PTR_TD);
	}
	splx(s);

	iso->next = -1;
	iso->inuse = 0;

	return (USBD_NORMAL_COMPLETION);

 bad:
	while (--i >= 0)
		uhci_free_std(sc, iso->stds[i]);
	free(iso->stds, M_USBHC, UHCI_VFRAMELIST_COUNT * sizeof(*iso->stds));
	return (USBD_NOMEM);
}

void
uhci_device_isoc_done(struct usbd_xfer *xfer)
{
	struct uhci_xfer *ux = (struct uhci_xfer *)xfer;

	DPRINTFN(4, ("uhci_device_isoc_done: length=%d\n", xfer->actlen));

	if (!uhci_active_intr_list(ux))
		return;

#ifdef DIAGNOSTIC
        if (ux->stdend == NULL) {
                printf("uhci_device_isoc_done: xfer=%p stdend==NULL\n", xfer);
#ifdef UHCI_DEBUG
		uhci_dump_xfer(ux);
#endif
		return;
	}
#endif

	/* Turn off the interrupt since it is active even if the TD is not. */
	ux->stdend->td.td_status &= htole32(~UHCI_TD_IOC);

	uhci_del_intr_list(ux);	/* remove from active list */
}

void
uhci_device_intr_done(struct usbd_xfer *xfer)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;
	struct uhci_pipe *upipe = (struct uhci_pipe *)xfer->pipe;
	struct uhci_xfer *ux = (struct uhci_xfer *)xfer;
	struct uhci_soft_qh *sqh;
	int i, npoll;

	DPRINTFN(5, ("uhci_device_intr_done: length=%d\n", xfer->actlen));

	npoll = upipe->u.intr.npoll;
	for(i = 0; i < npoll; i++) {
		sqh = upipe->u.intr.qhs[i];
		sqh->elink = NULL;
		sqh->qh.qh_elink = htole32(UHCI_PTR_T);
	}
	uhci_free_std_chain(sc, ux->stdstart, NULL);

	/* XXX Wasteful. */
	if (xfer->pipe->repeat) {
		struct uhci_soft_td *data, *dataend;

		DPRINTFN(5,("uhci_device_intr_done: requeuing\n"));

		/* This alloc cannot fail since we freed the chain above. */
		uhci_alloc_std_chain(sc, xfer->length, xfer, &data, &dataend);
		dataend->td.td_status |= htole32(UHCI_TD_IOC);

#ifdef UHCI_DEBUG
		if (uhcidebug > 10) {
			DPRINTF(("uhci_device_intr_done: data(1)\n"));
			uhci_dump_tds(data);
			uhci_dump_qh(upipe->u.intr.qhs[0]);
		}
#endif

		ux->stdstart = data;
		ux->stdend = dataend;
#ifdef DIAGNOSTIC
		if (!ux->isdone) {
			printf("%s: not done, ux=%p\n", __func__, ux);
		}
		ux->isdone = 0;
#endif
		for (i = 0; i < npoll; i++) {
			sqh = upipe->u.intr.qhs[i];
			sqh->elink = data;
			sqh->qh.qh_elink = htole32(data->physaddr | UHCI_PTR_TD);
		}
		xfer->status = USBD_IN_PROGRESS;
		/* The ux is already on the examined list, just leave it. */
	} else {
		DPRINTFN(5,("uhci_device_intr_done: removing\n"));
		if (uhci_active_intr_list(ux))
			uhci_del_intr_list(ux);
	}
}

/* Deallocate request data structures */
void
uhci_device_ctrl_done(struct usbd_xfer *xfer)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;
	struct uhci_pipe *upipe = (struct uhci_pipe *)xfer->pipe;
	struct uhci_xfer *ux = (struct uhci_xfer *)xfer;

#ifdef DIAGNOSTIC
	if (!(xfer->rqflags & URQ_REQUEST))
		panic("uhci_device_ctrl_done: not a request");
#endif

	if (!uhci_active_intr_list(ux))
		return;

	uhci_del_intr_list(ux);	/* remove from active list */

	if (xfer->device->speed == USB_SPEED_LOW)
		uhci_remove_ls_ctrl(sc, upipe->u.ctl.sqh);
	else
		uhci_remove_hs_ctrl(sc, upipe->u.ctl.sqh);

	if (upipe->u.ctl.length != 0)
		uhci_free_std_chain(sc, ux->stdstart->link.std, ux->stdend);

	DPRINTFN(5, ("uhci_device_ctrl_done: length=%d\n", xfer->actlen));
}

/* Deallocate request data structures */
void
uhci_device_bulk_done(struct usbd_xfer *xfer)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;
	struct uhci_pipe *upipe = (struct uhci_pipe *)xfer->pipe;
	struct uhci_xfer *ux = (struct uhci_xfer *)xfer;

	DPRINTFN(5,("uhci_device_bulk_done: xfer=%p ux=%p sc=%p upipe=%p\n",
		    xfer, ux, sc, upipe));

	if (!uhci_active_intr_list(ux))
		return;

	uhci_del_intr_list(ux);	/* remove from active list */

	uhci_remove_bulk(sc, upipe->u.bulk.sqh);

	uhci_free_std_chain(sc, ux->stdstart, NULL);

	DPRINTFN(5, ("uhci_device_bulk_done: length=%d\n", xfer->actlen));
}

/* Add interrupt QH, called with vflock. */
void
uhci_add_intr(struct uhci_softc *sc, struct uhci_soft_qh *sqh)
{
	struct uhci_vframe *vf = &sc->sc_vframes[sqh->pos];
	struct uhci_soft_qh *eqh;

	DPRINTFN(4, ("uhci_add_intr: n=%d sqh=%p\n", sqh->pos, sqh));

	eqh = vf->eqh;
	sqh->hlink       = eqh->hlink;
	sqh->qh.qh_hlink = eqh->qh.qh_hlink;
	eqh->hlink       = sqh;
	eqh->qh.qh_hlink = htole32(sqh->physaddr | UHCI_PTR_QH);
	vf->eqh = sqh;
	vf->bandwidth++;
}

/* Remove interrupt QH. */
void
uhci_remove_intr(struct uhci_softc *sc, struct uhci_soft_qh *sqh)
{
	struct uhci_vframe *vf = &sc->sc_vframes[sqh->pos];
	struct uhci_soft_qh *pqh;

	DPRINTFN(4, ("uhci_remove_intr: n=%d sqh=%p\n", sqh->pos, sqh));

	/* See comment in uhci_remove_ctrl() */
	if (!(sqh->qh.qh_elink & htole32(UHCI_PTR_T))) {
		sqh->qh.qh_elink = htole32(UHCI_PTR_T);
		delay(UHCI_QH_REMOVE_DELAY);
	}

	pqh = uhci_find_prev_qh(vf->hqh, sqh);
	pqh->hlink       = sqh->hlink;
	pqh->qh.qh_hlink = sqh->qh.qh_hlink;
	delay(UHCI_QH_REMOVE_DELAY);
	if (vf->eqh == sqh)
		vf->eqh = pqh;
	vf->bandwidth--;
}

usbd_status
uhci_device_setintr(struct uhci_softc *sc, struct uhci_pipe *upipe, int ival)
{
	struct uhci_soft_qh *sqh, **qhs;
	int i, npoll, s;
	u_int bestbw, bw, bestoffs, offs;

	DPRINTFN(2, ("uhci_device_setintr: pipe=%p\n", upipe));
	if (ival == 0) {
		printf("uhci_device_setintr: 0 interval\n");
		return (USBD_INVAL);
	}

	if (ival > UHCI_VFRAMELIST_COUNT)
		ival = UHCI_VFRAMELIST_COUNT;
	npoll = (UHCI_VFRAMELIST_COUNT + ival - 1) / ival;
	DPRINTFN(2, ("uhci_device_setintr: ival=%d npoll=%d\n", ival, npoll));

	qhs = mallocarray(npoll, sizeof(*qhs), M_USBHC, M_NOWAIT);
	if (qhs == NULL)
		return (USBD_NOMEM);

	/*
	 * Figure out which offset in the schedule that has most
	 * bandwidth left over.
	 */
#define MOD(i) ((i) & (UHCI_VFRAMELIST_COUNT-1))
	for (bestoffs = offs = 0, bestbw = ~0; offs < ival; offs++) {
		for (bw = i = 0; i < npoll; i++)
			bw += sc->sc_vframes[MOD(i * ival + offs)].bandwidth;
		if (bw < bestbw) {
			bestbw = bw;
			bestoffs = offs;
		}
	}
	DPRINTFN(1, ("uhci_device_setintr: bw=%d offs=%d\n", bestbw, bestoffs));

	for(i = 0; i < npoll; i++) {
		sqh = uhci_alloc_sqh(sc);
		if (sqh == NULL) {
			while (i > 0)
				uhci_free_sqh(sc, qhs[--i]);
			free(qhs, M_USBHC, npoll * sizeof(*qhs));
			return (USBD_NOMEM);
		}
		sqh->elink = NULL;
		sqh->qh.qh_elink = htole32(UHCI_PTR_T);
		sqh->pos = MOD(i * ival + bestoffs);
		qhs[i] = sqh;
	}
#undef MOD

	upipe->u.intr.npoll = npoll;
	upipe->u.intr.qhs = qhs;

	s = splusb();
	/* Enter QHs into the controller data structures. */
	for(i = 0; i < npoll; i++)
		uhci_add_intr(sc, upipe->u.intr.qhs[i]);
	splx(s);

	DPRINTFN(5, ("uhci_device_setintr: returns %p\n", upipe));
	return (USBD_NORMAL_COMPLETION);
}

/* Open a new pipe. */
usbd_status
uhci_open(struct usbd_pipe *pipe)
{
	struct uhci_softc *sc = (struct uhci_softc *)pipe->device->bus;
	struct uhci_pipe *upipe = (struct uhci_pipe *)pipe;
	usb_endpoint_descriptor_t *ed = pipe->endpoint->edesc;
	usbd_status err;
	int ival;

	DPRINTFN(1, ("uhci_open: pipe=%p, addr=%d, endpt=%d\n",
		     pipe, pipe->device->address, ed->bEndpointAddress));

	upipe->nexttoggle = pipe->endpoint->savedtoggle;

	/* Root Hub */
	if (pipe->device->depth == 0) {
		switch (ed->bEndpointAddress) {
		case USB_CONTROL_ENDPOINT:
			pipe->methods = &uhci_root_ctrl_methods;
			break;
		case UE_DIR_IN | UHCI_INTR_ENDPT:
			pipe->methods = &uhci_root_intr_methods;
			break;
		default:
			return (USBD_INVAL);
		}
	} else {
		switch (UE_GET_XFERTYPE(ed->bmAttributes)) {
		case UE_CONTROL:
			pipe->methods = &uhci_device_ctrl_methods;
			upipe->u.ctl.sqh = uhci_alloc_sqh(sc);
			if (upipe->u.ctl.sqh == NULL)
				goto bad;
			upipe->u.ctl.setup = uhci_alloc_std(sc);
			if (upipe->u.ctl.setup == NULL) {
				uhci_free_sqh(sc, upipe->u.ctl.sqh);
				goto bad;
			}
			upipe->u.ctl.stat = uhci_alloc_std(sc);
			if (upipe->u.ctl.stat == NULL) {
				uhci_free_sqh(sc, upipe->u.ctl.sqh);
				uhci_free_std(sc, upipe->u.ctl.setup);
				goto bad;
			}
			err = usb_allocmem(&sc->sc_bus,
				  sizeof(usb_device_request_t),
				  0, USB_DMA_COHERENT,
				  &upipe->u.ctl.reqdma);
			if (err) {
				uhci_free_sqh(sc, upipe->u.ctl.sqh);
				uhci_free_std(sc, upipe->u.ctl.setup);
				uhci_free_std(sc, upipe->u.ctl.stat);
				goto bad;
			}
			break;
		case UE_INTERRUPT:
			pipe->methods = &uhci_device_intr_methods;
			ival = pipe->interval;
			if (ival == USBD_DEFAULT_INTERVAL)
				ival = ed->bInterval;
			return (uhci_device_setintr(sc, upipe, ival));
		case UE_ISOCHRONOUS:
			pipe->methods = &uhci_device_isoc_methods;
			return (uhci_setup_isoc(pipe));
		case UE_BULK:
			pipe->methods = &uhci_device_bulk_methods;
			upipe->u.bulk.sqh = uhci_alloc_sqh(sc);
			if (upipe->u.bulk.sqh == NULL)
				goto bad;
			break;
		}
	}
	return (USBD_NORMAL_COMPLETION);

 bad:
	return (USBD_NOMEM);
}

/*
 * Data structures and routines to emulate the root hub.
 */
const usb_device_descriptor_t uhci_devd = {
	USB_DEVICE_DESCRIPTOR_SIZE,
	UDESC_DEVICE,		/* type */
	{0x00, 0x01},		/* USB version */
	UDCLASS_HUB,		/* class */
	UDSUBCLASS_HUB,		/* subclass */
	UDPROTO_FSHUB,		/* protocol */
	64,			/* max packet */
	{0},{0},{0x00,0x01},	/* device id */
	1,2,0,			/* string indices */
	1			/* # of configurations */
};

const usb_config_descriptor_t uhci_confd = {
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

const usb_interface_descriptor_t uhci_ifcd = {
	USB_INTERFACE_DESCRIPTOR_SIZE,
	UDESC_INTERFACE,
	0,
	0,
	1,
	UICLASS_HUB,
	UISUBCLASS_HUB,
	UIPROTO_FSHUB,
	0
};

const usb_endpoint_descriptor_t uhci_endpd = {
	USB_ENDPOINT_DESCRIPTOR_SIZE,
	UDESC_ENDPOINT,
	UE_DIR_IN | UHCI_INTR_ENDPT,
	UE_INTERRUPT,
	{8},
	255
};

const usb_hub_descriptor_t uhci_hubd_piix = {
	USB_HUB_DESCRIPTOR_SIZE,
	UDESC_HUB,
	2,
	{ UHD_PWR_NO_SWITCH | UHD_OC_INDIVIDUAL, 0 },
	50,			/* power on to power good */
	0,
	{ 0x00 },		/* both ports are removable */
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
usbd_status
uhci_portreset(struct uhci_softc *sc, int index)
{
	int lim, port, x;

	if (index == 1)
		port = UHCI_PORTSC1;
	else if (index == 2)
		port = UHCI_PORTSC2;
	else
		return (USBD_IOERROR);

	x = URWMASK(UREAD2(sc, port));
	UWRITE2(sc, port, x | UHCI_PORTSC_PR);

	usb_delay_ms(&sc->sc_bus, USB_PORT_ROOT_RESET_DELAY);

	DPRINTFN(3,("uhci port %d reset, status0 = 0x%04x\n",
		    index, UREAD2(sc, port)));

	x = URWMASK(UREAD2(sc, port));
	UWRITE2(sc, port, x & ~UHCI_PORTSC_PR);

	delay(100);

	DPRINTFN(3,("uhci port %d reset, status1 = 0x%04x\n",
		    index, UREAD2(sc, port)));

	x = URWMASK(UREAD2(sc, port));
	UWRITE2(sc, port, x  | UHCI_PORTSC_PE);

	for (lim = 10; --lim > 0;) {
		usb_delay_ms(&sc->sc_bus, USB_PORT_RESET_DELAY);

		x = UREAD2(sc, port);

		DPRINTFN(3,("uhci port %d iteration %u, status = 0x%04x\n",
			    index, lim, x));

		if (!(x & UHCI_PORTSC_CCS)) {
			/*
			 * No device is connected (or was disconnected
			 * during reset).  Consider the port reset.
			 * The delay must be long enough to ensure on
			 * the initial iteration that the device
			 * connection will have been registered.  50ms
			 * appears to be sufficient, but 20ms is not.
			 */
			DPRINTFN(3,("uhci port %d loop %u, device detached\n",
				    index, lim));
			break;
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

		if (x & UHCI_PORTSC_PE)
			/* Port is enabled */
			break;

		UWRITE2(sc, port, URWMASK(x) | UHCI_PORTSC_PE);
	}

	DPRINTFN(3,("uhci port %d reset, status2 = 0x%04x\n",
		    index, UREAD2(sc, port)));

	if (lim <= 0) {
		DPRINTFN(1,("uhci port %d reset timed out\n", index));
		return (USBD_TIMEOUT);
	}
	
	sc->sc_isreset = 1;
	return (USBD_NORMAL_COMPLETION);
}

/*
 * Simulate a hardware hub by handling all the necessary requests.
 */
usbd_status
uhci_root_ctrl_transfer(struct usbd_xfer *xfer)
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
	return (uhci_root_ctrl_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
uhci_root_ctrl_start(struct usbd_xfer *xfer)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;
	usb_device_request_t *req;
	void *buf = NULL;
	int port, x;
	int s, len, value, index, status, change, l, totlen = 0;
	usb_port_status_t ps;
	usb_device_descriptor_t devd;
	usbd_status err;

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

#ifdef DIAGNOSTIC
	if (!(xfer->rqflags & URQ_REQUEST))
		panic("uhci_root_ctrl_start: not a request");
#endif
	req = &xfer->request;

	DPRINTFN(2,("uhci_root_ctrl_start type=0x%02x request=%02x\n",
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
		DPRINTFN(2,("uhci_root_ctrl_start wValue=0x%04x\n", value));
		switch(value >> 8) {
		case UDESC_DEVICE:
			if ((value & 0xff) != 0) {
				err = USBD_IOERROR;
				goto ret;
			}
			devd = uhci_devd;
			USETW(devd.idVendor, sc->sc_id_vendor);
			totlen = l = min(len, USB_DEVICE_DESCRIPTOR_SIZE);
			memcpy(buf, &devd, l);
			break;
		case UDESC_CONFIG:
			if ((value & 0xff) != 0) {
				err = USBD_IOERROR;
				goto ret;
			}
			totlen = l = min(len, USB_CONFIG_DESCRIPTOR_SIZE);
			memcpy(buf, &uhci_confd, l);
			buf = (char *)buf + l;
			len -= l;
			l = min(len, USB_INTERFACE_DESCRIPTOR_SIZE);
			totlen += l;
			memcpy(buf, &uhci_ifcd, l);
			buf = (char *)buf + l;
			len -= l;
			l = min(len, USB_ENDPOINT_DESCRIPTOR_SIZE);
			totlen += l;
			memcpy(buf, &uhci_endpd, l);
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
				totlen = usbd_str(buf, len, "UHCI root hub");
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
		DPRINTFN(3, ("uhci_root_ctrl_start: UR_CLEAR_PORT_FEATURE "
			     "port=%d feature=%d\n",
			     index, value));
		if (index == 1)
			port = UHCI_PORTSC1;
		else if (index == 2)
			port = UHCI_PORTSC2;
		else {
			err = USBD_IOERROR;
			goto ret;
		}
		switch(value) {
		case UHF_PORT_ENABLE:
			x = URWMASK(UREAD2(sc, port));
			UWRITE2(sc, port, x & ~UHCI_PORTSC_PE);
			break;
		case UHF_PORT_SUSPEND:
			x = URWMASK(UREAD2(sc, port));
			UWRITE2(sc, port, x & ~UHCI_PORTSC_SUSP);
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
			err = USBD_NORMAL_COMPLETION;
			goto ret;
		case UHF_PORT_CONNECTION:
		case UHF_PORT_OVER_CURRENT:
		case UHF_PORT_POWER:
		case UHF_PORT_LOW_SPEED:
		case UHF_C_PORT_SUSPEND:
		default:
			err = USBD_IOERROR;
			goto ret;
		}
		break;
	case C(UR_GET_BUS_STATE, UT_READ_CLASS_OTHER):
		if (index == 1)
			port = UHCI_PORTSC1;
		else if (index == 2)
			port = UHCI_PORTSC2;
		else {
			err = USBD_IOERROR;
			goto ret;
		}
		if (len > 0) {
			*(u_int8_t *)buf =
				(UREAD2(sc, port) & UHCI_PORTSC_LS) >>
				UHCI_PORTSC_LS_SHIFT;
			totlen = 1;
		}
		break;
	case C(UR_GET_DESCRIPTOR, UT_READ_CLASS_DEVICE):
		if ((value & 0xff) != 0) {
			err = USBD_IOERROR;
			goto ret;
		}
		l = min(len, USB_HUB_DESCRIPTOR_SIZE);
		totlen = l;
		memcpy(buf, &uhci_hubd_piix, l);
		break;
	case C(UR_GET_STATUS, UT_READ_CLASS_DEVICE):
		if (len != 4) {
			err = USBD_IOERROR;
			goto ret;
		}
		memset(buf, 0, len);
		totlen = len;
		break;
	case C(UR_GET_STATUS, UT_READ_CLASS_OTHER):
		if (index == 1)
			port = UHCI_PORTSC1;
		else if (index == 2)
			port = UHCI_PORTSC2;
		else {
			err = USBD_IOERROR;
			goto ret;
		}
		if (len != 4) {
			err = USBD_IOERROR;
			goto ret;
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
		if (x & UHCI_PORTSC_SUSP)
			status |= UPS_SUSPEND;
		if (x & UHCI_PORTSC_LSDA)
			status |= UPS_LOW_SPEED;
		status |= UPS_PORT_POWER;
		if (sc->sc_isreset)
			change |= UPS_C_PORT_RESET;
		USETW(ps.wPortStatus, status);
		USETW(ps.wPortChange, change);
		l = min(len, sizeof ps);
		memcpy(buf, &ps, l);
		totlen = l;
		break;
	case C(UR_SET_DESCRIPTOR, UT_WRITE_CLASS_DEVICE):
		err = USBD_IOERROR;
		goto ret;
	case C(UR_SET_FEATURE, UT_WRITE_CLASS_DEVICE):
		break;
	case C(UR_SET_FEATURE, UT_WRITE_CLASS_OTHER):
		if (index == 1)
			port = UHCI_PORTSC1;
		else if (index == 2)
			port = UHCI_PORTSC2;
		else {
			err = USBD_IOERROR;
			goto ret;
		}
		switch(value) {
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
			goto ret;
		case UHF_PORT_POWER:
			/* Pretend we turned on power */
			err = USBD_NORMAL_COMPLETION;
			goto ret;
		case UHF_PORT_DISOWN_TO_1_1:
			/* accept, but do nothing */
			err = USBD_NORMAL_COMPLETION;
			goto ret;
		case UHF_C_PORT_CONNECTION:
		case UHF_C_PORT_ENABLE:
		case UHF_C_PORT_OVER_CURRENT:
		case UHF_PORT_CONNECTION:
		case UHF_PORT_OVER_CURRENT:
		case UHF_PORT_LOW_SPEED:
		case UHF_C_PORT_SUSPEND:
		case UHF_C_PORT_RESET:
		default:
			err = USBD_IOERROR;
			goto ret;
		}
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

/* Abort a root control request. */
void
uhci_root_ctrl_abort(struct usbd_xfer *xfer)
{
	/* Nothing to do, all transfers are synchronous. */
}

/* Close the root pipe. */
void
uhci_root_ctrl_close(struct usbd_pipe *pipe)
{
	DPRINTF(("uhci_root_ctrl_close\n"));
}

void
uhci_root_intr_abort(struct usbd_xfer *xfer)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;
	int s;

	timeout_del(&sc->sc_root_intr);
	sc->sc_intrxfer = NULL;

	xfer->status = USBD_CANCELLED;
	s = splusb();
	usb_transfer_complete(xfer);
	splx(s);
}

usbd_status
uhci_root_intr_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	/* Insert last in queue. */
	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	/* Pipe isn't running (otherwise err would be USBD_INPROG),
	 * start first
	 */
	return (uhci_root_intr_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

/* Start a transfer on the root interrupt pipe */
usbd_status
uhci_root_intr_start(struct usbd_xfer *xfer)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	sc->sc_intrxfer = xfer;
	timeout_add_msec(&sc->sc_root_intr, 255);

	return (USBD_IN_PROGRESS);
}

void
uhci_root_intr_close(struct usbd_pipe *pipe)
{
}

void
uhci_root_intr_done(struct usbd_xfer *xfer)
{
	struct uhci_softc *sc = (struct uhci_softc *)xfer->device->bus;

	if (xfer->pipe->repeat)
		timeout_add_msec(&sc->sc_root_intr, 255);
}
