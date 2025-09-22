/*	$OpenBSD: ohci.c,v 1.165 2022/04/12 19:41:11 naddy Exp $ */
/*	$NetBSD: ohci.c,v 1.139 2003/02/22 05:24:16 tsutsui Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/ohci.c,v 1.22 1999/11/17 22:33:40 n_hibma Exp $	*/

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

#include <dev/usb/ohcireg.h>
#include <dev/usb/ohcivar.h>

struct cfdriver ohci_cd = {
	NULL, "ohci", DV_DULL, CD_SKIPHIBERNATE
};

#ifdef OHCI_DEBUG
#define DPRINTF(x)	do { if (ohcidebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (ohcidebug>(n)) printf x; } while (0)
int ohcidebug = 0;
#define bitmask_snprintf(q,f,b,l) snprintf((b), (l), "%b", (q), (f))
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

struct pool *ohcixfer;

struct ohci_pipe;

struct ohci_soft_ed *ohci_alloc_sed(struct ohci_softc *);
void		ohci_free_sed(struct ohci_softc *, struct ohci_soft_ed *);

struct ohci_soft_td *ohci_alloc_std(struct ohci_softc *);
void		ohci_free_std(struct ohci_softc *, struct ohci_soft_td *);

struct ohci_soft_itd *ohci_alloc_sitd(struct ohci_softc *);
void		ohci_free_sitd(struct ohci_softc *, struct ohci_soft_itd *);

#if 0
void		ohci_free_std_chain(struct ohci_softc *, struct ohci_soft_td *,
		    struct ohci_soft_td *);
#endif
usbd_status	ohci_alloc_std_chain(struct ohci_softc *, u_int,
		    struct usbd_xfer *, struct ohci_soft_td *,
		    struct ohci_soft_td **);

usbd_status	ohci_open(struct usbd_pipe *);
int		ohci_setaddr(struct usbd_device *, int);
void		ohci_poll(struct usbd_bus *);
void		ohci_softintr(void *);
void		ohci_add_done(struct ohci_softc *, ohci_physaddr_t);
void		ohci_rhsc(struct ohci_softc *, struct usbd_xfer *);

usbd_status	ohci_device_request(struct usbd_xfer *xfer);
void		ohci_add_ed(struct ohci_soft_ed *, struct ohci_soft_ed *);
void		ohci_rem_ed(struct ohci_soft_ed *, struct ohci_soft_ed *);
void		ohci_hash_add_td(struct ohci_softc *, struct ohci_soft_td *);
struct ohci_soft_td *ohci_hash_find_td(struct ohci_softc *, ohci_physaddr_t);
void		ohci_hash_add_itd(struct ohci_softc *, struct ohci_soft_itd *);
void		ohci_hash_rem_itd(struct ohci_softc *, struct ohci_soft_itd *);
struct ohci_soft_itd *ohci_hash_find_itd(struct ohci_softc *, ohci_physaddr_t);

usbd_status	ohci_setup_isoc(struct usbd_pipe *pipe);
void		ohci_device_isoc_enter(struct usbd_xfer *);

struct usbd_xfer *ohci_allocx(struct usbd_bus *);
void		ohci_freex(struct usbd_bus *, struct usbd_xfer *);

usbd_status	ohci_root_ctrl_transfer(struct usbd_xfer *);
usbd_status	ohci_root_ctrl_start(struct usbd_xfer *);
void		ohci_root_ctrl_abort(struct usbd_xfer *);
void		ohci_root_ctrl_close(struct usbd_pipe *);
void		ohci_root_ctrl_done(struct usbd_xfer *);

usbd_status	ohci_root_intr_transfer(struct usbd_xfer *);
usbd_status	ohci_root_intr_start(struct usbd_xfer *);
void		ohci_root_intr_abort(struct usbd_xfer *);
void		ohci_root_intr_close(struct usbd_pipe *);
void		ohci_root_intr_done(struct usbd_xfer *);

usbd_status	ohci_device_ctrl_transfer(struct usbd_xfer *);
usbd_status	ohci_device_ctrl_start(struct usbd_xfer *);
void		ohci_device_ctrl_abort(struct usbd_xfer *);
void		ohci_device_ctrl_close(struct usbd_pipe *);
void		ohci_device_ctrl_done(struct usbd_xfer *);

usbd_status	ohci_device_bulk_transfer(struct usbd_xfer *);
usbd_status	ohci_device_bulk_start(struct usbd_xfer *);
void		ohci_device_bulk_abort(struct usbd_xfer *);
void		ohci_device_bulk_close(struct usbd_pipe *);
void		ohci_device_bulk_done(struct usbd_xfer *);

usbd_status	ohci_device_intr_transfer(struct usbd_xfer *);
usbd_status	ohci_device_intr_start(struct usbd_xfer *);
void		ohci_device_intr_abort(struct usbd_xfer *);
void		ohci_device_intr_close(struct usbd_pipe *);
void		ohci_device_intr_done(struct usbd_xfer *);

usbd_status	ohci_device_isoc_transfer(struct usbd_xfer *);
usbd_status	ohci_device_isoc_start(struct usbd_xfer *);
void		ohci_device_isoc_abort(struct usbd_xfer *);
void		ohci_device_isoc_close(struct usbd_pipe *);
void		ohci_device_isoc_done(struct usbd_xfer *);

usbd_status	ohci_device_setintr(struct ohci_softc *sc,
			    struct ohci_pipe *pipe, int ival);

void		ohci_timeout(void *);
void		ohci_timeout_task(void *);
void		ohci_rhsc_able(struct ohci_softc *, int);
void		ohci_rhsc_enable(void *);

void		ohci_close_pipe(struct usbd_pipe *, struct ohci_soft_ed *);
void		ohci_abort_xfer(struct usbd_xfer *, usbd_status);

void		ohci_device_clear_toggle(struct usbd_pipe *pipe);

#ifdef OHCI_DEBUG
void		ohci_dumpregs(struct ohci_softc *);
void		ohci_dump_tds(struct ohci_soft_td *);
void		ohci_dump_td(struct ohci_soft_td *);
void		ohci_dump_ed(struct ohci_soft_ed *);
void		ohci_dump_itd(struct ohci_soft_itd *);
void		ohci_dump_itds(struct ohci_soft_itd *);
#endif

#define OBARR(sc) bus_space_barrier((sc)->iot, (sc)->ioh, 0, (sc)->sc_size, \
			BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE)
#define OWRITE1(sc, r, x) \
 do { OBARR(sc); bus_space_write_1((sc)->iot, (sc)->ioh, (r), (x)); } while (0)
#define OWRITE2(sc, r, x) \
 do { OBARR(sc); bus_space_write_2((sc)->iot, (sc)->ioh, (r), (x)); } while (0)
#define OWRITE4(sc, r, x) \
 do { OBARR(sc); bus_space_write_4((sc)->iot, (sc)->ioh, (r), (x)); } while (0)

__unused static __inline u_int8_t
OREAD1(struct ohci_softc *sc, bus_size_t r)
{
	OBARR(sc);
	return bus_space_read_1(sc->iot, sc->ioh, r);
}

__unused static __inline u_int16_t
OREAD2(struct ohci_softc *sc, bus_size_t r)
{
	OBARR(sc);
	return bus_space_read_2(sc->iot, sc->ioh, r);
}

__unused static __inline u_int32_t
OREAD4(struct ohci_softc *sc, bus_size_t r)
{
	OBARR(sc);
	return bus_space_read_4(sc->iot, sc->ioh, r);
}

/* Reverse the bits in a value 0 .. 31 */
const u_int8_t revbits[OHCI_NO_INTRS] =
  { 0x00, 0x10, 0x08, 0x18, 0x04, 0x14, 0x0c, 0x1c,
    0x02, 0x12, 0x0a, 0x1a, 0x06, 0x16, 0x0e, 0x1e,
    0x01, 0x11, 0x09, 0x19, 0x05, 0x15, 0x0d, 0x1d,
    0x03, 0x13, 0x0b, 0x1b, 0x07, 0x17, 0x0f, 0x1f };

struct ohci_pipe {
	struct usbd_pipe pipe;
	struct ohci_soft_ed *sed;
	union {
		struct ohci_soft_td *td;
		struct ohci_soft_itd *itd;
	} tail;
	union {
		/* Control pipe */
		struct {
			struct usb_dma reqdma;
		} ctl;
		/* Interrupt pipe */
		struct {
			int nslots;
			int pos;
		} intr;
		/* Iso pipe */
		struct iso {
			int next, inuse;
		} iso;
	} u;
};

#define OHCI_INTR_ENDPT 1

const struct usbd_bus_methods ohci_bus_methods = {
	.open_pipe = ohci_open,
	.dev_setaddr = ohci_setaddr,
	.soft_intr = ohci_softintr,
	.do_poll = ohci_poll,
	.allocx = ohci_allocx,
	.freex = ohci_freex,
};

const struct usbd_pipe_methods ohci_root_ctrl_methods = {
	.transfer = ohci_root_ctrl_transfer,
	.start = ohci_root_ctrl_start,
	.abort = ohci_root_ctrl_abort,
	.close = ohci_root_ctrl_close,
	.done = ohci_root_ctrl_done,
};

const struct usbd_pipe_methods ohci_root_intr_methods = {
	.transfer = ohci_root_intr_transfer,
	.start = ohci_root_intr_start,
	.abort = ohci_root_intr_abort,
	.close = ohci_root_intr_close,
	.done = ohci_root_intr_done,
};

const struct usbd_pipe_methods ohci_device_ctrl_methods = {
	.transfer = ohci_device_ctrl_transfer,
	.start = ohci_device_ctrl_start,
	.abort = ohci_device_ctrl_abort,
	.close = ohci_device_ctrl_close,
	.done = ohci_device_ctrl_done,
};

const struct usbd_pipe_methods ohci_device_intr_methods = {
	.transfer = ohci_device_intr_transfer,
	.start = ohci_device_intr_start,
	.abort = ohci_device_intr_abort,
	.close = ohci_device_intr_close,
	.cleartoggle = ohci_device_clear_toggle,
	.done = ohci_device_intr_done,
};

const struct usbd_pipe_methods ohci_device_bulk_methods = {
	.transfer = ohci_device_bulk_transfer,
	.start = ohci_device_bulk_start,
	.abort = ohci_device_bulk_abort,
	.close = ohci_device_bulk_close,
	.cleartoggle = ohci_device_clear_toggle,
	.done = ohci_device_bulk_done,
};

const struct usbd_pipe_methods ohci_device_isoc_methods = {
	.transfer = ohci_device_isoc_transfer,
	.start = ohci_device_isoc_start,
	.abort = ohci_device_isoc_abort,
	.close = ohci_device_isoc_close,
	.done = ohci_device_isoc_done,
};

int
ohci_activate(struct device *self, int act)
{
	struct ohci_softc *sc = (struct ohci_softc *)self;
	u_int32_t reg;
	int rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
		sc->sc_bus.use_polling++;
		reg = OREAD4(sc, OHCI_CONTROL) & ~OHCI_HCFS_MASK;
		if (sc->sc_control == 0) {
			/*
			 * Preserve register values, in case that APM BIOS
			 * does not recover them.
			 */
			sc->sc_control = reg;
			sc->sc_intre = OREAD4(sc, OHCI_INTERRUPT_ENABLE);
			sc->sc_ival = OHCI_GET_IVAL(OREAD4(sc,
			    OHCI_FM_INTERVAL));
		}
		reg |= OHCI_HCFS_SUSPEND;
		OWRITE4(sc, OHCI_CONTROL, reg);
		usb_delay_ms(&sc->sc_bus, USB_RESUME_WAIT);
		sc->sc_bus.use_polling--;
		break;
	case DVACT_RESUME:
		sc->sc_bus.use_polling++;

		/* Some broken BIOSes do not recover these values */
		OWRITE4(sc, OHCI_HCCA, DMAADDR(&sc->sc_hccadma, 0));
		OWRITE4(sc, OHCI_CONTROL_HEAD_ED, sc->sc_ctrl_head->physaddr);
		OWRITE4(sc, OHCI_BULK_HEAD_ED, sc->sc_bulk_head->physaddr);
		if (sc->sc_intre)
			OWRITE4(sc, OHCI_INTERRUPT_ENABLE,
			    sc->sc_intre & (OHCI_ALL_INTRS | OHCI_MIE));
		if (sc->sc_control)
			reg = sc->sc_control;
		else
			reg = OREAD4(sc, OHCI_CONTROL);
		reg |= OHCI_HCFS_RESUME;
		OWRITE4(sc, OHCI_CONTROL, reg);
		usb_delay_ms(&sc->sc_bus, USB_RESUME_DELAY);
		reg = (reg & ~OHCI_HCFS_MASK) | OHCI_HCFS_OPERATIONAL;
		OWRITE4(sc, OHCI_CONTROL, reg);

		reg = (OREAD4(sc, OHCI_FM_REMAINING) & OHCI_FIT) ^ OHCI_FIT;
		reg |= OHCI_FSMPS(sc->sc_ival) | sc->sc_ival;
		OWRITE4(sc, OHCI_FM_INTERVAL, reg);
		OWRITE4(sc, OHCI_PERIODIC_START, OHCI_PERIODIC(sc->sc_ival));

		/* Fiddle the No OverCurrent Protection to avoid a chip bug */
		reg = OREAD4(sc, OHCI_RH_DESCRIPTOR_A);
		OWRITE4(sc, OHCI_RH_DESCRIPTOR_A, reg | OHCI_NOCP);
		OWRITE4(sc, OHCI_RH_STATUS, OHCI_LPSC); /* Enable port power */
		usb_delay_ms(&sc->sc_bus, OHCI_ENABLE_POWER_DELAY);
		OWRITE4(sc, OHCI_RH_DESCRIPTOR_A, reg);

		usb_delay_ms(&sc->sc_bus, USB_RESUME_RECOVERY);
		sc->sc_control = sc->sc_intre = sc->sc_ival = 0;
		sc->sc_bus.use_polling--;
		rv = config_activate_children(self, act);
		break;
	case DVACT_POWERDOWN:
		rv = config_activate_children(self, act);
		OWRITE4(sc, OHCI_CONTROL, OHCI_HCFS_RESET);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

int
ohci_detach(struct device *self, int flags)
{
	struct ohci_softc *sc = (struct ohci_softc *)self;
	int rv;

	rv = config_detach_children(self, flags);
	if (rv != 0)
		return (rv);

	timeout_del(&sc->sc_tmo_rhsc);

	usb_delay_ms(&sc->sc_bus, 300); /* XXX let stray task complete */

	/* free data structures XXX */

	return (rv);
}

struct ohci_soft_ed *
ohci_alloc_sed(struct ohci_softc *sc)
{
	struct ohci_soft_ed *sed = NULL;
	usbd_status err;
	int i, offs;
	struct usb_dma dma;
	int s;

	s = splusb();
	if (sc->sc_freeeds == NULL) {
		DPRINTFN(2, ("ohci_alloc_sed: allocating chunk\n"));
		err = usb_allocmem(&sc->sc_bus, OHCI_SED_SIZE * OHCI_SED_CHUNK,
			  OHCI_ED_ALIGN, USB_DMA_COHERENT, &dma);
		if (err)
			goto out;
		for (i = 0; i < OHCI_SED_CHUNK; i++) {
			offs = i * OHCI_SED_SIZE;
			sed = KERNADDR(&dma, offs);
			sed->physaddr = DMAADDR(&dma, offs);
			sed->next = sc->sc_freeeds;
			sc->sc_freeeds = sed;
		}
	}
	sed = sc->sc_freeeds;
	sc->sc_freeeds = sed->next;
	memset(&sed->ed, 0, sizeof(struct ohci_ed));
	sed->next = NULL;

out:
	splx(s);
	return (sed);
}

void
ohci_free_sed(struct ohci_softc *sc, struct ohci_soft_ed *sed)
{
	int s;

	s = splusb();
	sed->next = sc->sc_freeeds;
	sc->sc_freeeds = sed;
	splx(s);
}

struct ohci_soft_td *
ohci_alloc_std(struct ohci_softc *sc)
{
	struct ohci_soft_td *std = NULL;
	usbd_status err;
	int i, offs;
	struct usb_dma dma;
	int s;

	s = splusb();
	if (sc->sc_freetds == NULL) {
		DPRINTFN(2, ("ohci_alloc_std: allocating chunk\n"));
		err = usb_allocmem(&sc->sc_bus, OHCI_STD_SIZE * OHCI_STD_CHUNK,
			  OHCI_TD_ALIGN, USB_DMA_COHERENT, &dma);
		if (err)
			goto out;
		for (i = 0; i < OHCI_STD_CHUNK; i++) {
			offs = i * OHCI_STD_SIZE;
			std = KERNADDR(&dma, offs);
			std->physaddr = DMAADDR(&dma, offs);
			std->nexttd = sc->sc_freetds;
			sc->sc_freetds = std;
		}
	}

	std = sc->sc_freetds;
	sc->sc_freetds = std->nexttd;
	memset(&std->td, 0, sizeof(struct ohci_td));
	std->nexttd = NULL;
	std->xfer = NULL;
	ohci_hash_add_td(sc, std);

out:
	splx(s);
	return (std);
}

void
ohci_free_std(struct ohci_softc *sc, struct ohci_soft_td *std)
{
	int s;

	s = splusb();
	LIST_REMOVE(std, hnext);
	std->nexttd = sc->sc_freetds;
	sc->sc_freetds = std;
	splx(s);
}

usbd_status
ohci_alloc_std_chain(struct ohci_softc *sc, u_int alen, struct usbd_xfer *xfer,
    struct ohci_soft_td *sp, struct ohci_soft_td **ep)
{
	struct ohci_soft_td *next, *cur, *end;
	ohci_physaddr_t dataphys, dataphysend;
	u_int32_t tdflags;
	u_int len, curlen;
	int mps;
	int rd = usbd_xfer_isread(xfer);
	struct usb_dma *dma = &xfer->dmabuf;
	u_int16_t flags = xfer->flags;

	DPRINTFN(alen < 4096,("ohci_alloc_std_chain: start len=%u\n", alen));

	usb_syncmem(&xfer->dmabuf, 0, xfer->length,
	    usbd_xfer_isread(xfer) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	len = alen;
	cur = sp;
	end = NULL;

	dataphys = DMAADDR(dma, 0);
	dataphysend = OHCI_PAGE(dataphys + len - 1);
	tdflags = htole32(
	    (rd ? OHCI_TD_IN : OHCI_TD_OUT) |
	    (flags & USBD_SHORT_XFER_OK ? OHCI_TD_R : 0) |
	    OHCI_TD_NOCC | OHCI_TD_TOGGLE_CARRY | OHCI_TD_NOINTR);
	mps = UGETW(xfer->pipe->endpoint->edesc->wMaxPacketSize);

	while (len > 0) {
		next = ohci_alloc_std(sc);
		if (next == NULL)
			goto nomem;

		/* The OHCI hardware can handle at most one page crossing. */
		if (OHCI_PAGE(dataphys) == dataphysend ||
		    OHCI_PAGE(dataphys) + OHCI_PAGE_SIZE == dataphysend) {
			/* we can handle it in this TD */
			curlen = len;
		} else {
			/* must use multiple TDs, fill as much as possible. */
			curlen = 2 * OHCI_PAGE_SIZE -
				 (dataphys & (OHCI_PAGE_SIZE-1));
			/* the length must be a multiple of the max size */
			curlen -= curlen % mps;
#ifdef DIAGNOSTIC
			if (curlen == 0)
				panic("ohci_alloc_std: curlen == 0");
#endif
		}
		DPRINTFN(4,("ohci_alloc_std_chain: dataphys=0x%08x "
			    "dataphysend=0x%08x len=%u curlen=%u\n",
			    dataphys, dataphysend,
			    len, curlen));
		len -= curlen;

		cur->td.td_flags = tdflags;
		cur->td.td_cbp = htole32(dataphys);
		cur->nexttd = next;
		cur->td.td_nexttd = htole32(next->physaddr);
		cur->td.td_be = htole32(dataphys + curlen - 1);
		cur->len = curlen;
		cur->flags = OHCI_ADD_LEN;
		cur->xfer = xfer;
		DPRINTFN(10,("ohci_alloc_std_chain: cbp=0x%08x be=0x%08x\n",
			    dataphys, dataphys + curlen - 1));
		DPRINTFN(10,("ohci_alloc_std_chain: extend chain\n"));
		dataphys += curlen;
		end = cur;
		cur = next;
	}
	if (!rd && ((flags & USBD_FORCE_SHORT_XFER) || alen == 0) &&
	    alen % mps == 0) {
		/* Force a 0 length transfer at the end. */

		next = ohci_alloc_std(sc);
		if (next == NULL)
			goto nomem;

		cur->td.td_flags = tdflags;
		cur->td.td_cbp = 0; /* indicate 0 length packet */
		cur->nexttd = next;
		cur->td.td_nexttd = htole32(next->physaddr);
		cur->td.td_be = ~0;
		cur->len = 0;
		cur->flags = 0;
		cur->xfer = xfer;
		DPRINTFN(2,("ohci_alloc_std_chain: add 0 xfer\n"));
		end = cur;
	}
	*ep = end;

	return (USBD_NORMAL_COMPLETION);

 nomem:
	/* XXX free chain */
	return (USBD_NOMEM);
}

#if 0
void
ohci_free_std_chain(struct ohci_softc *sc, struct ohci_soft_td *std,
    struct ohci_soft_td *stdend)
{
	struct ohci_soft_td *p;

	for (; std != stdend; std = p) {
		p = std->nexttd;
		ohci_free_std(sc, std);
	}
}
#endif

struct ohci_soft_itd *
ohci_alloc_sitd(struct ohci_softc *sc)
{
	struct ohci_soft_itd *sitd;
	usbd_status err;
	int i, s, offs;
	struct usb_dma dma;

	if (sc->sc_freeitds == NULL) {
		DPRINTFN(2, ("ohci_alloc_sitd: allocating chunk\n"));
		err = usb_allocmem(&sc->sc_bus, OHCI_SITD_SIZE * OHCI_SITD_CHUNK,
			  OHCI_ITD_ALIGN, USB_DMA_COHERENT, &dma);
		if (err)
			return (NULL);
		s = splusb();
		for(i = 0; i < OHCI_SITD_CHUNK; i++) {
			offs = i * OHCI_SITD_SIZE;
			sitd = KERNADDR(&dma, offs);
			sitd->physaddr = DMAADDR(&dma, offs);
			sitd->nextitd = sc->sc_freeitds;
			sc->sc_freeitds = sitd;
		}
		splx(s);
	}

	s = splusb();
	sitd = sc->sc_freeitds;
	sc->sc_freeitds = sitd->nextitd;
	memset(&sitd->itd, 0, sizeof(struct ohci_itd));
	sitd->nextitd = NULL;
	sitd->xfer = NULL;
	ohci_hash_add_itd(sc, sitd);
	splx(s);

#ifdef DIAGNOSTIC
	sitd->isdone = 0;
#endif

	return (sitd);
}

void
ohci_free_sitd(struct ohci_softc *sc, struct ohci_soft_itd *sitd)
{
	int s;

	DPRINTFN(10,("ohci_free_sitd: sitd=%p\n", sitd));

#ifdef DIAGNOSTIC
	if (!sitd->isdone) {
		panic("ohci_free_sitd: sitd=%p not done", sitd);
		return;
	}
	/* Warn double free */
	sitd->isdone = 0;
#endif

	s = splusb();
	ohci_hash_rem_itd(sc, sitd);
	sitd->nextitd = sc->sc_freeitds;
	sc->sc_freeitds = sitd;
	splx(s);
}

usbd_status
ohci_checkrev(struct ohci_softc *sc)
{
	u_int32_t rev;

	rev = OREAD4(sc, OHCI_REVISION);
	printf("version %d.%d%s\n", OHCI_REV_HI(rev), OHCI_REV_LO(rev),
	       OHCI_REV_LEGACY(rev) ? ", legacy support" : "");

	if (OHCI_REV_HI(rev) != 1 || OHCI_REV_LO(rev) != 0) {
		printf("%s: unsupported OHCI revision\n",
		       sc->sc_bus.bdev.dv_xname);
		sc->sc_bus.usbrev = USBREV_UNKNOWN;
		return (USBD_INVAL);
	}
	sc->sc_bus.usbrev = USBREV_1_0;

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
ohci_handover(struct ohci_softc *sc)
{
	u_int32_t s, ctl;
	int i;

	ctl = OREAD4(sc, OHCI_CONTROL);
	if (ctl & OHCI_IR) {
		/* SMM active, request change */
		DPRINTF(("ohci_handover: SMM active, request owner change\n"));
		if ((sc->sc_intre & (OHCI_OC | OHCI_MIE)) == 
		    (OHCI_OC | OHCI_MIE))
			OWRITE4(sc, OHCI_INTERRUPT_ENABLE, OHCI_MIE);
		s = OREAD4(sc, OHCI_COMMAND_STATUS);
		OWRITE4(sc, OHCI_COMMAND_STATUS, s | OHCI_OCR);
		for (i = 0; i < 100 && (ctl & OHCI_IR); i++) {
			usb_delay_ms(&sc->sc_bus, 1);
			ctl = OREAD4(sc, OHCI_CONTROL);
		}
		OWRITE4(sc, OHCI_INTERRUPT_DISABLE, OHCI_MIE);
		if (ctl & OHCI_IR) {
			printf("%s: SMM does not respond, will reset\n",
			    sc->sc_bus.bdev.dv_xname);
		}
	}

	return (USBD_NORMAL_COMPLETION);
}

usbd_status
ohci_init(struct ohci_softc *sc)
{
	struct ohci_soft_ed *sed, *psed;
	usbd_status err;
	int i;
	u_int32_t ctl, rwc, ival, hcr, fm, per, desca, descb;

	DPRINTF(("ohci_init: start\n"));

	for (i = 0; i < OHCI_HASH_SIZE; i++)
		LIST_INIT(&sc->sc_hash_tds[i]);
	for (i = 0; i < OHCI_HASH_SIZE; i++)
		LIST_INIT(&sc->sc_hash_itds[i]);

	if (ohcixfer == NULL) {
		ohcixfer = malloc(sizeof(struct pool), M_USBHC, M_NOWAIT);
		if (ohcixfer == NULL) {
			printf("%s: unable to allocate pool descriptor\n",
			    sc->sc_bus.bdev.dv_xname);
			return (ENOMEM);
		}
		pool_init(ohcixfer, sizeof(struct ohci_xfer), 0, IPL_SOFTUSB,
		    0, "ohcixfer", NULL);
	}

	/* XXX determine alignment by R/W */
	/* Allocate the HCCA area. */
	err = usb_allocmem(&sc->sc_bus, OHCI_HCCA_SIZE, OHCI_HCCA_ALIGN,
	    USB_DMA_COHERENT, &sc->sc_hccadma);
	if (err)
		return (err);
	sc->sc_hcca = KERNADDR(&sc->sc_hccadma, 0);
	memset(sc->sc_hcca, 0, OHCI_HCCA_SIZE);

	sc->sc_eintrs = OHCI_NORMAL_INTRS;

	/* Allocate dummy ED that starts the control list. */
	sc->sc_ctrl_head = ohci_alloc_sed(sc);
	if (sc->sc_ctrl_head == NULL) {
		err = USBD_NOMEM;
		goto bad1;
	}
	sc->sc_ctrl_head->ed.ed_flags |= htole32(OHCI_ED_SKIP);

	/* Allocate dummy ED that starts the bulk list. */
	sc->sc_bulk_head = ohci_alloc_sed(sc);
	if (sc->sc_bulk_head == NULL) {
		err = USBD_NOMEM;
		goto bad2;
	}
	sc->sc_bulk_head->ed.ed_flags |= htole32(OHCI_ED_SKIP);

	/* Allocate dummy ED that starts the isochronous list. */
	sc->sc_isoc_head = ohci_alloc_sed(sc);
	if (sc->sc_isoc_head == NULL) {
		err = USBD_NOMEM;
		goto bad3;
	}
	sc->sc_isoc_head->ed.ed_flags |= htole32(OHCI_ED_SKIP);

	/* Allocate all the dummy EDs that make up the interrupt tree. */
	for (i = 0; i < OHCI_NO_EDS; i++) {
		sed = ohci_alloc_sed(sc);
		if (sed == NULL) {
			while (--i >= 0)
				ohci_free_sed(sc, sc->sc_eds[i]);
			err = USBD_NOMEM;
			goto bad4;
		}
		/* All ED fields are set to 0. */
		sc->sc_eds[i] = sed;
		sed->ed.ed_flags |= htole32(OHCI_ED_SKIP);
		if (i != 0)
			psed = sc->sc_eds[(i-1) / 2];
		else
			psed= sc->sc_isoc_head;
		sed->next = psed;
		sed->ed.ed_nexted = htole32(psed->physaddr);
	}
	/*
	 * Fill HCCA interrupt table.  The bit reversal is to get
	 * the tree set up properly to spread the interrupts.
	 */
	for (i = 0; i < OHCI_NO_INTRS; i++)
		sc->sc_hcca->hcca_interrupt_table[revbits[i]] =
		    htole32(sc->sc_eds[OHCI_NO_EDS-OHCI_NO_INTRS+i]->physaddr);

#ifdef OHCI_DEBUG
	if (ohcidebug > 15) {
		for (i = 0; i < OHCI_NO_EDS; i++) {
			printf("ed#%d ", i);
			ohci_dump_ed(sc->sc_eds[i]);
		}
		printf("iso ");
		ohci_dump_ed(sc->sc_isoc_head);
	}
#endif
	/* Preserve values programmed by SMM/BIOS but lost over reset. */
	ctl = OREAD4(sc, OHCI_CONTROL);
	rwc = ctl & OHCI_RWC;
	fm = OREAD4(sc, OHCI_FM_INTERVAL);
	desca = OREAD4(sc, OHCI_RH_DESCRIPTOR_A);
	descb = OREAD4(sc, OHCI_RH_DESCRIPTOR_B);

	/* Determine in what context we are running. */
	if (ctl & OHCI_IR) {
		OWRITE4(sc, OHCI_CONTROL, OHCI_HCFS_RESET | rwc);
		goto reset;
#if 0
/* Don't bother trying to reuse the BIOS init, we'll reset it anyway. */
	} else if ((ctl & OHCI_HCFS_MASK) != OHCI_HCFS_RESET) {
		/* BIOS started controller. */
		DPRINTF(("ohci_init: BIOS active\n"));
		if ((ctl & OHCI_HCFS_MASK) != OHCI_HCFS_OPERATIONAL) {
			OWRITE4(sc, OHCI_CONTROL, OHCI_HCFS_OPERATIONAL | rwc);
			usb_delay_ms(&sc->sc_bus, USB_RESUME_DELAY);
		}
#endif
	} else {
		DPRINTF(("ohci_init: cold started\n"));
	reset:
		/* Controller was cold started. */
		usb_delay_ms(&sc->sc_bus, USB_BUS_RESET_DELAY);
	}

	/*
	 * This reset should not be necessary according to the OHCI spec, but
	 * without it some controllers do not start.
	 */
	DPRINTF(("%s: resetting\n", sc->sc_bus.bdev.dv_xname));
	OWRITE4(sc, OHCI_CONTROL, OHCI_HCFS_RESET | rwc);
	usb_delay_ms(&sc->sc_bus, USB_BUS_RESET_DELAY);

	/* We now own the host controller and the bus has been reset. */

	OWRITE4(sc, OHCI_COMMAND_STATUS, OHCI_HCR); /* Reset HC */
	/* Nominal time for a reset is 10 us. */
	for (i = 0; i < 10; i++) {
		delay(10);
		hcr = OREAD4(sc, OHCI_COMMAND_STATUS) & OHCI_HCR;
		if (!hcr)
			break;
	}
	if (hcr) {
		printf("%s: reset timeout\n", sc->sc_bus.bdev.dv_xname);
		err = USBD_IOERROR;
		goto bad5;
	}
#ifdef OHCI_DEBUG
	if (ohcidebug > 15)
		ohci_dumpregs(sc);
#endif

	/* The controller is now in SUSPEND state, we have 2ms to finish. */

	/* Set up HC registers. */
	OWRITE4(sc, OHCI_HCCA, DMAADDR(&sc->sc_hccadma, 0));
	OWRITE4(sc, OHCI_CONTROL_HEAD_ED, sc->sc_ctrl_head->physaddr);
	OWRITE4(sc, OHCI_BULK_HEAD_ED, sc->sc_bulk_head->physaddr);
	/* disable all interrupts and then switch on all desired interrupts */
	OWRITE4(sc, OHCI_INTERRUPT_DISABLE, OHCI_ALL_INTRS);
	/* switch on desired functional features */
	ctl = OREAD4(sc, OHCI_CONTROL);
	ctl &= ~(OHCI_CBSR_MASK | OHCI_LES | OHCI_HCFS_MASK | OHCI_IR);
	ctl |= OHCI_PLE | OHCI_IE | OHCI_CLE | OHCI_BLE |
		OHCI_RATIO_1_4 | OHCI_HCFS_OPERATIONAL | rwc;
	/* And finally start it! */
	OWRITE4(sc, OHCI_CONTROL, ctl);

	/*
	 * The controller is now OPERATIONAL.  Set a some final
	 * registers that should be set earlier, but that the
	 * controller ignores when in the SUSPEND state.
	 */
	ival = OHCI_GET_IVAL(fm);
	fm = (OREAD4(sc, OHCI_FM_REMAINING) & OHCI_FIT) ^ OHCI_FIT;
	fm |= OHCI_FSMPS(ival) | ival;
	OWRITE4(sc, OHCI_FM_INTERVAL, fm);
	per = OHCI_PERIODIC(ival); /* 90% periodic */
	OWRITE4(sc, OHCI_PERIODIC_START, per);

	/* Fiddle the No OverCurrent Protection bit to avoid chip bug. */
	OWRITE4(sc, OHCI_RH_DESCRIPTOR_A, desca | OHCI_NOCP);
	OWRITE4(sc, OHCI_RH_STATUS, OHCI_LPSC); /* Enable port power */
	usb_delay_ms(&sc->sc_bus, OHCI_ENABLE_POWER_DELAY);
	OWRITE4(sc, OHCI_RH_DESCRIPTOR_A, desca);
	OWRITE4(sc, OHCI_RH_DESCRIPTOR_B, descb);
	usb_delay_ms(&sc->sc_bus, OHCI_GET_POTPGT(desca) * UHD_PWRON_FACTOR);

	/*
	 * The AMD756 requires a delay before re-reading the register,
	 * otherwise it will occasionally report 0 ports.
	 */
	sc->sc_noport = 0;
	for (i = 0; i < 10 && sc->sc_noport == 0; i++) {
		usb_delay_ms(&sc->sc_bus, OHCI_READ_DESC_DELAY);
		sc->sc_noport = OHCI_GET_NDP(OREAD4(sc, OHCI_RH_DESCRIPTOR_A));
	}

#ifdef OHCI_DEBUG
	if (ohcidebug > 5)
		ohci_dumpregs(sc);
#endif

	/* Set up the bus struct. */
	sc->sc_bus.methods = &ohci_bus_methods;
	sc->sc_bus.pipe_size = sizeof(struct ohci_pipe);

	sc->sc_control = sc->sc_intre = 0;

	timeout_set(&sc->sc_tmo_rhsc, ohci_rhsc_enable, sc);

	/* Finally, turn on interrupts. */
	DPRINTFN(1,("ohci_init: enabling\n"));
	OWRITE4(sc, OHCI_INTERRUPT_ENABLE, sc->sc_eintrs | OHCI_MIE);

	return (USBD_NORMAL_COMPLETION);

 bad5:
	for (i = 0; i < OHCI_NO_EDS; i++)
		ohci_free_sed(sc, sc->sc_eds[i]);
 bad4:
	ohci_free_sed(sc, sc->sc_isoc_head);
 bad3:
	ohci_free_sed(sc, sc->sc_bulk_head);
 bad2:
	ohci_free_sed(sc, sc->sc_ctrl_head);
 bad1:
	usb_freemem(&sc->sc_bus, &sc->sc_hccadma);
	return (err);
}

struct usbd_xfer *
ohci_allocx(struct usbd_bus *bus)
{
	return (pool_get(ohcixfer, PR_NOWAIT | PR_ZERO));
}

void
ohci_freex(struct usbd_bus *bus, struct usbd_xfer *xfer)
{
	pool_put(ohcixfer, xfer);
}

#ifdef OHCI_DEBUG
void
ohci_dumpregs(struct ohci_softc *sc)
{
	DPRINTF(("ohci_dumpregs: rev=0x%08x control=0x%08x command=0x%08x\n",
		 OREAD4(sc, OHCI_REVISION),
		 OREAD4(sc, OHCI_CONTROL),
		 OREAD4(sc, OHCI_COMMAND_STATUS)));
	DPRINTF(("               intrstat=0x%08x intre=0x%08x intrd=0x%08x\n",
		 OREAD4(sc, OHCI_INTERRUPT_STATUS),
		 OREAD4(sc, OHCI_INTERRUPT_ENABLE),
		 OREAD4(sc, OHCI_INTERRUPT_DISABLE)));
	DPRINTF(("               hcca=0x%08x percur=0x%08x ctrlhd=0x%08x\n",
		 OREAD4(sc, OHCI_HCCA),
		 OREAD4(sc, OHCI_PERIOD_CURRENT_ED),
		 OREAD4(sc, OHCI_CONTROL_HEAD_ED)));
	DPRINTF(("               ctrlcur=0x%08x bulkhd=0x%08x bulkcur=0x%08x\n",
		 OREAD4(sc, OHCI_CONTROL_CURRENT_ED),
		 OREAD4(sc, OHCI_BULK_HEAD_ED),
		 OREAD4(sc, OHCI_BULK_CURRENT_ED)));
	DPRINTF(("               done=0x%08x fmival=0x%08x fmrem=0x%08x\n",
		 OREAD4(sc, OHCI_DONE_HEAD),
		 OREAD4(sc, OHCI_FM_INTERVAL),
		 OREAD4(sc, OHCI_FM_REMAINING)));
	DPRINTF(("               fmnum=0x%08x perst=0x%08x lsthrs=0x%08x\n",
		 OREAD4(sc, OHCI_FM_NUMBER),
		 OREAD4(sc, OHCI_PERIODIC_START),
		 OREAD4(sc, OHCI_LS_THRESHOLD)));
	DPRINTF(("               desca=0x%08x descb=0x%08x stat=0x%08x\n",
		 OREAD4(sc, OHCI_RH_DESCRIPTOR_A),
		 OREAD4(sc, OHCI_RH_DESCRIPTOR_B),
		 OREAD4(sc, OHCI_RH_STATUS)));
	DPRINTF(("               port1=0x%08x port2=0x%08x\n",
		 OREAD4(sc, OHCI_RH_PORT_STATUS(1)),
		 OREAD4(sc, OHCI_RH_PORT_STATUS(2))));
	DPRINTF(("         HCCA: frame_number=0x%04x done_head=0x%08x\n",
		 letoh32(sc->sc_hcca->hcca_frame_number),
		 letoh32(sc->sc_hcca->hcca_done_head)));
}
#endif

int ohci_intr1(struct ohci_softc *);

int
ohci_intr(void *p)
{
	struct ohci_softc *sc = p;

	if (sc == NULL || sc->sc_bus.dying)
		return (0);

	/* If we get an interrupt while polling, then just ignore it. */
	if (sc->sc_bus.use_polling) {
#ifdef DIAGNOSTIC
		static struct timeval ohci_intr_tv;
		if ((OREAD4(sc, OHCI_INTERRUPT_STATUS) & sc->sc_eintrs) &&
		    usbd_ratecheck(&ohci_intr_tv))
			DPRINTFN(16,
			    ("ohci_intr: ignored interrupt while polling\n"));
#endif
		return (0);
	}

	return (ohci_intr1(sc));
}

int
ohci_intr1(struct ohci_softc *sc)
{
	u_int32_t intrs, eintrs;
	ohci_physaddr_t done;

	DPRINTFN(14,("ohci_intr1: enter\n"));

	/* In case the interrupt occurs before initialization has completed. */
	if (sc == NULL || sc->sc_hcca == NULL) {
#ifdef DIAGNOSTIC
		printf("ohci_intr: sc->sc_hcca == NULL\n");
#endif
		return (0);
	}

        intrs = 0;
	done = letoh32(sc->sc_hcca->hcca_done_head);
	if (done != 0) {
		if (done & ~OHCI_DONE_INTRS)
			intrs = OHCI_WDH;
		if (done & OHCI_DONE_INTRS)
			intrs |= OREAD4(sc, OHCI_INTERRUPT_STATUS);
		sc->sc_hcca->hcca_done_head = 0;
	} else {
		intrs = OREAD4(sc, OHCI_INTERRUPT_STATUS);
		/* If we've flushed out a WDH then reread */
		if (intrs & OHCI_WDH) {
			done = letoh32(sc->sc_hcca->hcca_done_head);
			sc->sc_hcca->hcca_done_head = 0;
		}
	}

	if (intrs == 0xffffffff) {
		sc->sc_bus.dying = 1;
		return (0);
	}

	if (!intrs)
		return (0);

	intrs &= ~OHCI_MIE;
	OWRITE4(sc, OHCI_INTERRUPT_STATUS, intrs); /* Acknowledge */
	eintrs = intrs & sc->sc_eintrs;
	if (!eintrs)
		return (0);

	sc->sc_bus.intr_context++;
	sc->sc_bus.no_intrs++;
	DPRINTFN(7, ("ohci_intr: sc=%p intrs=0x%x(0x%x) eintrs=0x%x\n",
		     sc, (u_int)intrs, OREAD4(sc, OHCI_INTERRUPT_STATUS),
		     (u_int)eintrs));

	if (eintrs & OHCI_SO) {
		sc->sc_overrun_cnt++;
		if (usbd_ratecheck(&sc->sc_overrun_ntc)) {
			printf("%s: %u scheduling overruns\n",
			    sc->sc_bus.bdev.dv_xname, sc->sc_overrun_cnt);
			sc->sc_overrun_cnt = 0;
		}
		/* XXX do what */
		eintrs &= ~OHCI_SO;
	}
	if (eintrs & OHCI_WDH) {
		ohci_add_done(sc, done &~ OHCI_DONE_INTRS);
		usb_schedsoftintr(&sc->sc_bus);
		eintrs &= ~OHCI_WDH;
	}
	if (eintrs & OHCI_RD) {
		printf("%s: resume detect\n", sc->sc_bus.bdev.dv_xname);
		/* XXX process resume detect */
	}
	if (eintrs & OHCI_UE) {
		printf("%s: unrecoverable error, controller halted\n",
		       sc->sc_bus.bdev.dv_xname);
		OWRITE4(sc, OHCI_CONTROL, OHCI_HCFS_RESET);
		/* XXX what else */
	}
	if (eintrs & OHCI_RHSC) {
		ohci_rhsc(sc, sc->sc_intrxfer);
		/*
		 * Disable RHSC interrupt for now, because it will be
		 * on until the port has been reset.
		 */
		ohci_rhsc_able(sc, 0);
		DPRINTFN(2, ("%s: rhsc interrupt disabled\n",
			     sc->sc_bus.bdev.dv_xname));

		/* Do not allow RHSC interrupts > 1 per second */
                timeout_add_sec(&sc->sc_tmo_rhsc, 1);
		eintrs &= ~OHCI_RHSC;
	}

	sc->sc_bus.intr_context--;

	if (eintrs != 0) {
		/* Block unprocessed interrupts. XXX */
		OWRITE4(sc, OHCI_INTERRUPT_DISABLE, eintrs);
		sc->sc_eintrs &= ~eintrs;
		printf("%s: blocking intrs 0x%x\n",
		       sc->sc_bus.bdev.dv_xname, eintrs);
	}

	return (1);
}

void
ohci_rhsc_able(struct ohci_softc *sc, int on)
{
	DPRINTFN(4, ("ohci_rhsc_able: on=%d\n", on));
	if (on) {
		sc->sc_eintrs |= OHCI_RHSC;
		OWRITE4(sc, OHCI_INTERRUPT_ENABLE, OHCI_RHSC);
	} else {
		sc->sc_eintrs &= ~OHCI_RHSC;
		OWRITE4(sc, OHCI_INTERRUPT_DISABLE, OHCI_RHSC);
	}
}

void
ohci_rhsc_enable(void *v_sc)
{
	struct ohci_softc *sc = v_sc;
	int s;

	if (sc->sc_bus.dying)
		return;

	s = splhardusb();
	ohci_rhsc(sc, sc->sc_intrxfer);
	DPRINTFN(2, ("%s: rhsc interrupt enabled\n",
		     sc->sc_bus.bdev.dv_xname));

	ohci_rhsc_able(sc, 1);
	splx(s);
}

#ifdef OHCI_DEBUG
const char *ohci_cc_strs[] = {
	"NO_ERROR",
	"CRC",
	"BIT_STUFFING",
	"DATA_TOGGLE_MISMATCH",
	"STALL",
	"DEVICE_NOT_RESPONDING",
	"PID_CHECK_FAILURE",
	"UNEXPECTED_PID",
	"DATA_OVERRUN",
	"DATA_UNDERRUN",
	"BUFFER_OVERRUN",
	"BUFFER_UNDERRUN",
	"reserved",
	"reserved",
	"NOT_ACCESSED",
	"NOT_ACCESSED",
};
#endif

void
ohci_add_done(struct ohci_softc *sc, ohci_physaddr_t done)
{
	struct ohci_soft_itd *sitd, *sidone, **ip;
	struct ohci_soft_td *std, *sdone, **p;

	/* Reverse the done list. */
	for (sdone = NULL, sidone = NULL; done != 0; ) {
		std = ohci_hash_find_td(sc, done);
		if (std != NULL) {
			std->dnext = sdone;
			done = letoh32(std->td.td_nexttd);
			sdone = std;
			DPRINTFN(10,("add TD %p\n", std));
			continue;
		}
		sitd = ohci_hash_find_itd(sc, done);
		if (sitd != NULL) {
			sitd->dnext = sidone;
			done = letoh32(sitd->itd.itd_nextitd);
			sidone = sitd;
			DPRINTFN(5,("add ITD %p\n", sitd));
			continue;
		}
		panic("ohci_add_done: addr 0x%08lx not found", (u_long)done);
	}

	/* sdone & sidone now hold the done lists. */
	/* Put them on the already processed lists. */
	for (p = &sc->sc_sdone; *p != NULL; p = &(*p)->dnext)
		;
	*p = sdone;
	for (ip = &sc->sc_sidone; *ip != NULL; ip = &(*ip)->dnext)
		;
	*ip = sidone;
}

void
ohci_softintr(void *v)
{
	struct ohci_softc *sc = v;
	struct ohci_soft_itd *sitd, *sidone, *sitdnext;
	struct ohci_soft_td *std, *sdone, *stdnext;
	struct usbd_xfer *xfer;
	struct ohci_pipe *opipe;
	int len, cc, s;
	int i, j, actlen, iframes, uedir;

	DPRINTFN(10,("ohci_softintr: enter\n"));

	if (sc->sc_bus.dying)
		return;

	sc->sc_bus.intr_context++;

	s = splhardusb();
	sdone = sc->sc_sdone;
	sc->sc_sdone = NULL;
	sidone = sc->sc_sidone;
	sc->sc_sidone = NULL;
	splx(s);

	DPRINTFN(10,("ohci_softintr: sdone=%p sidone=%p\n", sdone, sidone));

#ifdef OHCI_DEBUG
	if (ohcidebug > 10) {
		DPRINTF(("ohci_process_done: TD done:\n"));
		ohci_dump_tds(sdone);
	}
#endif

	for (std = sdone; std; std = stdnext) {
		xfer = std->xfer;
		stdnext = std->dnext;
		DPRINTFN(10, ("ohci_process_done: std=%p xfer=%p hcpriv=%p\n",
				std, xfer, xfer ? xfer->hcpriv : 0));
		if (xfer == NULL) {
			/*
			 * xfer == NULL: There seems to be no xfer associated
			 * with this TD. It is tailp that happened to end up on
			 * the done queue.
			 * Shouldn't happen, but some chips are broken(?).
			 */
			continue;
		}
		if (xfer->status == USBD_CANCELLED ||
		    xfer->status == USBD_TIMEOUT) {
			DPRINTF(("ohci_process_done: cancel/timeout %p\n",
				 xfer));
			/* Handled by abort routine. */
			continue;
		}
		timeout_del(&xfer->timeout_handle);
		usb_rem_task(xfer->device, &xfer->abort_task);

		len = std->len;
		if (std->td.td_cbp != 0)
			len -= letoh32(std->td.td_be) -
			    letoh32(std->td.td_cbp) + 1;
		DPRINTFN(10, ("ohci_process_done: len=%d, flags=0x%x\n", len,
		    std->flags));
		if (std->flags & OHCI_ADD_LEN)
			xfer->actlen += len;

		cc = OHCI_TD_GET_CC(letoh32(std->td.td_flags));
		if (cc == OHCI_CC_NO_ERROR) {
			int done = (std->flags & OHCI_CALL_DONE);

			ohci_free_std(sc, std);
			if (done) {
				if (xfer->actlen)
					usb_syncmem(&xfer->dmabuf, 0,
					    xfer->actlen,
					    usbd_xfer_isread(xfer) ?
					    BUS_DMASYNC_POSTREAD :
					    BUS_DMASYNC_POSTWRITE);
				xfer->status = USBD_NORMAL_COMPLETION;
				s = splusb();
				usb_transfer_complete(xfer);
				splx(s);
			}
		} else {
			/*
			 * Endpoint is halted.  First unlink all the TDs
			 * belonging to the failed transfer, and then restart
			 * the endpoint.
			 */
			struct ohci_soft_td *p, *n;
			opipe = (struct ohci_pipe *)xfer->pipe;

			DPRINTFN(15,("ohci_process_done: error cc=%d (%s)\n",
			  OHCI_TD_GET_CC(letoh32(std->td.td_flags)),
			  ohci_cc_strs[OHCI_TD_GET_CC(letoh32(std->td.td_flags))]));

			/* remove TDs */
			for (p = std; p->xfer == xfer; p = n) {
				n = p->nexttd;
				ohci_free_std(sc, p);
			}

			/* clear halt */
			opipe->sed->ed.ed_headp = htole32(p->physaddr);
			OWRITE4(sc, OHCI_COMMAND_STATUS, OHCI_CLF);

			if (cc == OHCI_CC_STALL)
				xfer->status = USBD_STALLED;
			else if (cc == OHCI_CC_DATA_UNDERRUN) {
				if (xfer->actlen)
					usb_syncmem(&xfer->dmabuf, 0,
					    xfer->actlen,
					    usbd_xfer_isread(xfer) ?
					    BUS_DMASYNC_POSTREAD :
					    BUS_DMASYNC_POSTWRITE);
				xfer->status = USBD_NORMAL_COMPLETION;
			} else
				xfer->status = USBD_IOERROR;
			s = splusb();
			usb_transfer_complete(xfer);
			splx(s);
		}
	}

#ifdef OHCI_DEBUG
	if (ohcidebug > 10) {
		DPRINTF(("ohci_softintr: ITD done:\n"));
		ohci_dump_itds(sidone);
	}
#endif

	for (sitd = sidone; sitd != NULL; sitd = sitdnext) {
		xfer = sitd->xfer;
		sitdnext = sitd->dnext;
		DPRINTFN(1, ("ohci_process_done: sitd=%p xfer=%p hcpriv=%p\n",
			     sitd, xfer, xfer ? xfer->hcpriv : 0));
		if (xfer == NULL)
			continue;
		if (xfer->status == USBD_CANCELLED ||
		    xfer->status == USBD_TIMEOUT) {
			DPRINTF(("ohci_process_done: cancel/timeout %p\n",
				 xfer));
			/* Handled by abort routine. */
			continue;
		}
#ifdef DIAGNOSTIC
		if (sitd->isdone)
			printf("ohci_softintr: sitd=%p is done\n", sitd);
		sitd->isdone = 1;
#endif
		if (sitd->flags & OHCI_CALL_DONE) {
			struct ohci_soft_itd *next;

			opipe = (struct ohci_pipe *)xfer->pipe;
			opipe->u.iso.inuse -= xfer->nframes;
			uedir = UE_GET_DIR(xfer->pipe->endpoint->edesc->
			    bEndpointAddress);
			xfer->status = USBD_NORMAL_COMPLETION;
			actlen = 0;
			for (i = 0, sitd = xfer->hcpriv; ;
			    sitd = next) {
				next = sitd->nextitd;
				if (OHCI_ITD_GET_CC(letoh32(sitd->
				    itd.itd_flags)) != OHCI_CC_NO_ERROR)
					xfer->status = USBD_IOERROR;
				/* For input, update frlengths with actual */
				/* XXX anything necessary for output? */
				if (uedir == UE_DIR_IN &&
				    xfer->status == USBD_NORMAL_COMPLETION) {
					iframes = OHCI_ITD_GET_FC(letoh32(
					    sitd->itd.itd_flags));
					for (j = 0; j < iframes; i++, j++) {
						len = letoh16(sitd->
						    itd.itd_offset[j]);
						if ((OHCI_ITD_PSW_GET_CC(len) &
						    OHCI_CC_NOT_ACCESSED_MASK)
						    == OHCI_CC_NOT_ACCESSED)
							len = 0;
						else
							len = OHCI_ITD_PSW_LENGTH(len);
						xfer->frlengths[i] = len;
						actlen += len;
					}
				}
				if (sitd->flags & OHCI_CALL_DONE)
					break;
				ohci_free_sitd(sc, sitd);
			}
			ohci_free_sitd(sc, sitd);
			if (uedir == UE_DIR_IN &&
			    xfer->status == USBD_NORMAL_COMPLETION)
				xfer->actlen = actlen;
			xfer->hcpriv = NULL;

			if (xfer->status == USBD_NORMAL_COMPLETION) {
				usb_syncmem(&xfer->dmabuf, 0, xfer->length,
				    usbd_xfer_isread(xfer) ?
				    BUS_DMASYNC_POSTREAD :
				    BUS_DMASYNC_POSTWRITE);
			}

			s = splusb();
			usb_transfer_complete(xfer);
			splx(s);
		}
	}

	if (sc->sc_softwake) {
		sc->sc_softwake = 0;
		wakeup(&sc->sc_softwake);
	}

	sc->sc_bus.intr_context--;
	DPRINTFN(10,("ohci_softintr: done:\n"));
}

void
ohci_device_ctrl_done(struct usbd_xfer *xfer)
{
	DPRINTFN(10,("ohci_device_ctrl_done: xfer=%p\n", xfer));

#ifdef DIAGNOSTIC
	if (!(xfer->rqflags & URQ_REQUEST)) {
		panic("ohci_device_ctrl_done: not a request");
	}
#endif
}

void
ohci_device_intr_done(struct usbd_xfer *xfer)
{
	struct ohci_softc *sc = (struct ohci_softc *)xfer->device->bus;
	struct ohci_pipe *opipe = (struct ohci_pipe *)xfer->pipe;
	struct ohci_soft_ed *sed = opipe->sed;
	struct ohci_soft_td *data, *tail;


	DPRINTFN(10, ("ohci_device_intr_done: xfer=%p, actlen=%d\n", xfer,
	    xfer->actlen));

	if (xfer->pipe->repeat) {
		data = opipe->tail.td;
		tail = ohci_alloc_std(sc);
		if (tail == NULL) {
			xfer->status = USBD_NOMEM;
			return;
		}
		tail->xfer = NULL;

		data->td.td_flags = htole32(
			OHCI_TD_IN | OHCI_TD_NOCC |
			OHCI_TD_SET_DI(1) | OHCI_TD_TOGGLE_CARRY);
		if (xfer->flags & USBD_SHORT_XFER_OK)
			data->td.td_flags |= htole32(OHCI_TD_R);
		data->td.td_cbp = htole32(DMAADDR(&xfer->dmabuf, 0));
		data->nexttd = tail;
		data->td.td_nexttd = htole32(tail->physaddr);
		data->td.td_be = htole32(letoh32(data->td.td_cbp) +
			xfer->length - 1);
		data->len = xfer->length;
		data->xfer = xfer;
		data->flags = OHCI_CALL_DONE | OHCI_ADD_LEN;
		xfer->hcpriv = data;
		xfer->actlen = 0;

		sed->ed.ed_tailp = htole32(tail->physaddr);
		opipe->tail.td = tail;
	}
}

void
ohci_device_bulk_done(struct usbd_xfer *xfer)
{
	DPRINTFN(10, ("ohci_device_bulk_done: xfer=%p, actlen=%d\n", xfer,
	    xfer->actlen));
}

void
ohci_rhsc(struct ohci_softc *sc, struct usbd_xfer *xfer)
{
	u_char *p;
	int i, m;
	int hstatus;

	hstatus = OREAD4(sc, OHCI_RH_STATUS);
	DPRINTF(("ohci_rhsc: sc=%p xfer=%p hstatus=0x%08x\n",
		 sc, xfer, hstatus));

	if (xfer == NULL) {
		/* Just ignore the change. */
		return;
	}

	p = KERNADDR(&xfer->dmabuf, 0);
	m = min(sc->sc_noport, xfer->length * 8 - 1);
	memset(p, 0, xfer->length);
	for (i = 1; i <= m; i++) {
		/* Pick out CHANGE bits from the status reg. */
		if (OREAD4(sc, OHCI_RH_PORT_STATUS(i)) >> 16)
			p[i/8] |= 1 << (i%8);
	}
	DPRINTF(("ohci_rhsc: change=0x%02x\n", *p));
	xfer->actlen = xfer->length;
	xfer->status = USBD_NORMAL_COMPLETION;

	usb_transfer_complete(xfer);
}

void
ohci_root_intr_done(struct usbd_xfer *xfer)
{
}

void
ohci_root_ctrl_done(struct usbd_xfer *xfer)
{
}

void
ohci_poll(struct usbd_bus *bus)
{
	struct ohci_softc *sc = (struct ohci_softc *)bus;
#ifdef OHCI_DEBUG
	static int last;
	int new;
	new = OREAD4(sc, OHCI_INTERRUPT_STATUS);
	if (new != last) {
		DPRINTFN(10,("ohci_poll: intrs=0x%04x\n", new));
		last = new;
	}
#endif

	if (OREAD4(sc, OHCI_INTERRUPT_STATUS) & sc->sc_eintrs)
		ohci_intr1(sc);
}

usbd_status
ohci_device_request(struct usbd_xfer *xfer)
{
	struct ohci_softc *sc = (struct ohci_softc *)xfer->device->bus;
	struct ohci_pipe *opipe = (struct ohci_pipe *)xfer->pipe;
	usb_device_request_t *req = &xfer->request;
	struct ohci_soft_td *setup, *stat, *next, *tail;
	struct ohci_soft_ed *sed;
	u_int len;
	usbd_status err;
	int s;

	len = UGETW(req->wLength);

	DPRINTFN(3,("ohci_device_control type=0x%02x, request=0x%02x, "
		    "wValue=0x%04x, wIndex=0x%04x len=%u, addr=%d, endpt=%d\n",
		    req->bmRequestType, req->bRequest, UGETW(req->wValue),
		    UGETW(req->wIndex), len, xfer->device->address,
		    xfer->pipe->endpoint->edesc->bEndpointAddress));

	setup = opipe->tail.td;
	stat = ohci_alloc_std(sc);
	if (stat == NULL) {
		err = USBD_NOMEM;
		goto bad1;
	}
	tail = ohci_alloc_std(sc);
	if (tail == NULL) {
		err = USBD_NOMEM;
		goto bad2;
	}
	tail->xfer = NULL;

	sed = opipe->sed;

	next = stat;

	/* Set up data transaction */
	if (len != 0) {
		struct ohci_soft_td *std = stat;

		err = ohci_alloc_std_chain(sc, len, xfer, std, &stat);
		stat = stat->nexttd; /* point at free TD */
		if (err)
			goto bad3;
		/* Start toggle at 1 and then use the carried toggle. */
		std->td.td_flags &= htole32(~OHCI_TD_TOGGLE_MASK);
		std->td.td_flags |= htole32(OHCI_TD_TOGGLE_1);
	}

	memcpy(KERNADDR(&opipe->u.ctl.reqdma, 0), req, sizeof *req);

	setup->td.td_flags = htole32(OHCI_TD_SETUP | OHCI_TD_NOCC |
				     OHCI_TD_TOGGLE_0 | OHCI_TD_NOINTR);
	setup->td.td_cbp = htole32(DMAADDR(&opipe->u.ctl.reqdma, 0));
	setup->nexttd = next;
	setup->td.td_nexttd = htole32(next->physaddr);
	setup->td.td_be = htole32(letoh32(setup->td.td_cbp) + sizeof *req - 1);
	setup->len = 0;
	setup->xfer = xfer;
	setup->flags = 0;
	xfer->hcpriv = setup;

	stat->td.td_flags = htole32(
		(usbd_xfer_isread(xfer) ? OHCI_TD_OUT : OHCI_TD_IN) |
		OHCI_TD_NOCC | OHCI_TD_TOGGLE_1 | OHCI_TD_SET_DI(1));
	stat->td.td_cbp = 0;
	stat->nexttd = tail;
	stat->td.td_nexttd = htole32(tail->physaddr);
	stat->td.td_be = 0;
	stat->flags = OHCI_CALL_DONE;
	stat->len = 0;
	stat->xfer = xfer;

#ifdef OHCI_DEBUG
	if (ohcidebug > 5) {
		DPRINTF(("ohci_device_request:\n"));
		ohci_dump_ed(sed);
		ohci_dump_tds(setup);
	}
#endif

	/* Insert ED in schedule */
	s = splusb();
	sed->ed.ed_tailp = htole32(tail->physaddr);
	opipe->tail.td = tail;
	OWRITE4(sc, OHCI_COMMAND_STATUS, OHCI_CLF);
	if (xfer->timeout && !sc->sc_bus.use_polling) {
                timeout_del(&xfer->timeout_handle);
                timeout_set(&xfer->timeout_handle, ohci_timeout, xfer);
                timeout_add_msec(&xfer->timeout_handle, xfer->timeout);
	}
	splx(s);

#ifdef OHCI_DEBUG
	if (ohcidebug > 20) {
		delay(10000);
		DPRINTF(("ohci_device_request: status=%x\n",
			 OREAD4(sc, OHCI_COMMAND_STATUS)));
		ohci_dumpregs(sc);
		printf("ctrl head:\n");
		ohci_dump_ed(sc->sc_ctrl_head);
		printf("sed:\n");
		ohci_dump_ed(sed);
		ohci_dump_tds(setup);
	}
#endif

	return (USBD_NORMAL_COMPLETION);

 bad3:
	ohci_free_std(sc, tail);
 bad2:
	ohci_free_std(sc, stat);
 bad1:
	return (err);
}

/*
 * Add an ED to the schedule.  Called at splusb().
 */
void
ohci_add_ed(struct ohci_soft_ed *sed, struct ohci_soft_ed *head)
{
	DPRINTFN(8,("ohci_add_ed: sed=%p head=%p\n", sed, head));

	splsoftassert(IPL_SOFTUSB);
	sed->next = head->next;
	sed->ed.ed_nexted = head->ed.ed_nexted;
	head->next = sed;
	head->ed.ed_nexted = htole32(sed->physaddr);
}

/*
 * Remove an ED from the schedule.  Called at splusb().
 */
void
ohci_rem_ed(struct ohci_soft_ed *sed, struct ohci_soft_ed *head)
{
	struct ohci_soft_ed *p;

	splsoftassert(IPL_SOFTUSB);

	/* XXX */
	for (p = head; p != NULL && p->next != sed; p = p->next)
		;
	if (p == NULL)
		panic("ohci_rem_ed: ED not found");
	p->next = sed->next;
	p->ed.ed_nexted = sed->ed.ed_nexted;
}

/*
 * When a transfer is completed the TD is added to the done queue by
 * the host controller.  This queue is the processed by software.
 * Unfortunately the queue contains the physical address of the TD
 * and we have no simple way to translate this back to a kernel address.
 * To make the translation possible (and fast) we use a hash table of
 * TDs currently in the schedule.  The physical address is used as the
 * hash value.
 */

#define HASH(a) (((a) >> 4) % OHCI_HASH_SIZE)
/* Called at splusb() */
void
ohci_hash_add_td(struct ohci_softc *sc, struct ohci_soft_td *std)
{
	int h = HASH(std->physaddr);

	splsoftassert(IPL_SOFTUSB);

	LIST_INSERT_HEAD(&sc->sc_hash_tds[h], std, hnext);
}

struct ohci_soft_td *
ohci_hash_find_td(struct ohci_softc *sc, ohci_physaddr_t a)
{
	int h = HASH(a);
	struct ohci_soft_td *std;

	for (std = LIST_FIRST(&sc->sc_hash_tds[h]);
	     std != NULL;
	     std = LIST_NEXT(std, hnext))
		if (std->physaddr == a)
			return (std);
	return (NULL);
}

/* Called at splusb() */
void
ohci_hash_add_itd(struct ohci_softc *sc, struct ohci_soft_itd *sitd)
{
	int h = HASH(sitd->physaddr);

	splsoftassert(IPL_SOFTUSB);

	DPRINTFN(10,("ohci_hash_add_itd: sitd=%p physaddr=0x%08lx\n",
		    sitd, (u_long)sitd->physaddr));

	LIST_INSERT_HEAD(&sc->sc_hash_itds[h], sitd, hnext);
}

/* Called at splusb() */
void
ohci_hash_rem_itd(struct ohci_softc *sc, struct ohci_soft_itd *sitd)
{
	splsoftassert(IPL_SOFTUSB);

	DPRINTFN(10,("ohci_hash_rem_itd: sitd=%p physaddr=0x%08lx\n",
		    sitd, (u_long)sitd->physaddr));

	LIST_REMOVE(sitd, hnext);
}

struct ohci_soft_itd *
ohci_hash_find_itd(struct ohci_softc *sc, ohci_physaddr_t a)
{
	int h = HASH(a);
	struct ohci_soft_itd *sitd;

	for (sitd = LIST_FIRST(&sc->sc_hash_itds[h]);
	     sitd != NULL;
	     sitd = LIST_NEXT(sitd, hnext))
		if (sitd->physaddr == a)
			return (sitd);
	return (NULL);
}

void
ohci_timeout(void *addr)
{
	struct usbd_xfer *xfer = addr;
	struct ohci_softc *sc = (struct ohci_softc *)xfer->device->bus;

	if (sc->sc_bus.dying) {
		ohci_timeout_task(addr);
		return;
	}

	usb_init_task(&xfer->abort_task, ohci_timeout_task, addr,
	    USB_TASK_TYPE_ABORT);
	usb_add_task(xfer->device, &xfer->abort_task);
}

void
ohci_timeout_task(void *addr)
{
	struct usbd_xfer *xfer = addr;
	int s;

	DPRINTF(("%s: xfer=%p\n", __func__, xfer));

	s = splusb();
	ohci_abort_xfer(xfer, USBD_TIMEOUT);
	splx(s);
}

#ifdef OHCI_DEBUG
void
ohci_dump_tds(struct ohci_soft_td *std)
{
	for (; std; std = std->nexttd)
		ohci_dump_td(std);
}

void
ohci_dump_td(struct ohci_soft_td *std)
{
	char sbuf[128];

	bitmask_snprintf((u_int32_t)letoh32(std->td.td_flags),
			 "\20\23R\24OUT\25IN\31TOG1\32SETTOGGLE",
			 sbuf, sizeof(sbuf));

	printf("TD(%p) at %08lx: %s delay=%d ec=%d cc=%d\ncbp=0x%08lx "
	       "nexttd=0x%08lx be=0x%08lx\n",
	       std, (u_long)std->physaddr, sbuf,
	       OHCI_TD_GET_DI(letoh32(std->td.td_flags)),
	       OHCI_TD_GET_EC(letoh32(std->td.td_flags)),
	       OHCI_TD_GET_CC(letoh32(std->td.td_flags)),
	       (u_long)letoh32(std->td.td_cbp),
	       (u_long)letoh32(std->td.td_nexttd),
	       (u_long)letoh32(std->td.td_be));
}

void
ohci_dump_itd(struct ohci_soft_itd *sitd)
{
	int i;

	printf("ITD(%p) at %08lx: sf=%d di=%d fc=%d cc=%d\n"
	       "bp0=0x%08lx next=0x%08lx be=0x%08lx\n",
	       sitd, (u_long)sitd->physaddr,
	       OHCI_ITD_GET_SF(letoh32(sitd->itd.itd_flags)),
	       OHCI_ITD_GET_DI(letoh32(sitd->itd.itd_flags)),
	       OHCI_ITD_GET_FC(letoh32(sitd->itd.itd_flags)),
	       OHCI_ITD_GET_CC(letoh32(sitd->itd.itd_flags)),
	       (u_long)letoh32(sitd->itd.itd_bp0),
	       (u_long)letoh32(sitd->itd.itd_nextitd),
	       (u_long)letoh32(sitd->itd.itd_be));
	for (i = 0; i < OHCI_ITD_NOFFSET; i++)
		printf("offs[%d]=0x%04x ", i,
		       (u_int)letoh16(sitd->itd.itd_offset[i]));
	printf("\n");
}

void
ohci_dump_itds(struct ohci_soft_itd *sitd)
{
	for (; sitd; sitd = sitd->nextitd)
		ohci_dump_itd(sitd);
}

void
ohci_dump_ed(struct ohci_soft_ed *sed)
{
	char sbuf[128], sbuf2[128];

	bitmask_snprintf((u_int32_t)letoh32(sed->ed.ed_flags),
			 "\20\14OUT\15IN\16LOWSPEED\17SKIP\20ISO",
			 sbuf, sizeof(sbuf));
	bitmask_snprintf((u_int32_t)letoh32(sed->ed.ed_headp),
			 "\20\1HALT\2CARRY", sbuf2, sizeof(sbuf2));

	printf("ED(%p) at 0x%08lx: addr=%d endpt=%d maxp=%d flags=%s\n"
	       "tailp=0x%08lx headflags=%s headp=0x%08lx nexted=0x%08lx\n",
	       sed, (u_long)sed->physaddr,
	       OHCI_ED_GET_FA(letoh32(sed->ed.ed_flags)),
	       OHCI_ED_GET_EN(letoh32(sed->ed.ed_flags)),
	       OHCI_ED_GET_MAXP(letoh32(sed->ed.ed_flags)), sbuf,
	       (u_long)letoh32(sed->ed.ed_tailp), sbuf2,
	       (u_long)letoh32(sed->ed.ed_headp),
	       (u_long)letoh32(sed->ed.ed_nexted));
}
#endif

usbd_status
ohci_open(struct usbd_pipe *pipe)
{
	struct ohci_softc *sc = (struct ohci_softc *)pipe->device->bus;
	usb_endpoint_descriptor_t *ed = pipe->endpoint->edesc;
	struct ohci_pipe *opipe = (struct ohci_pipe *)pipe;
	u_int8_t xfertype = UE_GET_XFERTYPE(ed->bmAttributes);
	struct ohci_soft_ed *sed = NULL;
	struct ohci_soft_td *std = NULL;
	struct ohci_soft_itd *sitd;
	ohci_physaddr_t tdphys;
	u_int32_t fmt;
	usbd_status err;
	int s;
	int ival;

	DPRINTFN(1, ("ohci_open: pipe=%p, addr=%d, endpt=%d\n",
		     pipe, pipe->device->address, ed->bEndpointAddress));

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	/* Root Hub */
	if (pipe->device->depth == 0) {
		switch (ed->bEndpointAddress) {
		case USB_CONTROL_ENDPOINT:
			pipe->methods = &ohci_root_ctrl_methods;
			break;
		case UE_DIR_IN | OHCI_INTR_ENDPT:
			pipe->methods = &ohci_root_intr_methods;
			break;
		default:
			return (USBD_INVAL);
		}
	} else {
		sed = ohci_alloc_sed(sc);
		if (sed == NULL)
			goto bad0;
		opipe->sed = sed;
		if (xfertype == UE_ISOCHRONOUS) {
			sitd = ohci_alloc_sitd(sc);
			if (sitd == NULL)
				goto bad1;
			opipe->tail.itd = sitd;
			tdphys = sitd->physaddr;
			fmt = OHCI_ED_FORMAT_ISO;
			if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN)
				fmt |= OHCI_ED_DIR_IN;
			else
				fmt |= OHCI_ED_DIR_OUT;
		} else {
			std = ohci_alloc_std(sc);
			if (std == NULL)
				goto bad1;
			opipe->tail.td = std;
			tdphys = std->physaddr;
			fmt = OHCI_ED_FORMAT_GEN | OHCI_ED_DIR_TD;
		}
		sed->ed.ed_flags = htole32(
			OHCI_ED_SET_FA(pipe->device->address) |
			OHCI_ED_SET_EN(UE_GET_ADDR(ed->bEndpointAddress)) |
			(pipe->device->speed == USB_SPEED_LOW ?
			     OHCI_ED_SPEED : 0) |
			fmt | OHCI_ED_SET_MAXP(UGETW(ed->wMaxPacketSize)));
		sed->ed.ed_headp = htole32(tdphys |
		    (pipe->endpoint->savedtoggle ? OHCI_TOGGLECARRY : 0));
		sed->ed.ed_tailp = htole32(tdphys);

		switch (xfertype) {
		case UE_CONTROL:
			pipe->methods = &ohci_device_ctrl_methods;
			err = usb_allocmem(&sc->sc_bus,
				  sizeof(usb_device_request_t),
				  0, USB_DMA_COHERENT,
				  &opipe->u.ctl.reqdma);
			if (err)
				goto bad;
			s = splusb();
			ohci_add_ed(sed, sc->sc_ctrl_head);
			splx(s);
			break;
		case UE_INTERRUPT:
			pipe->methods = &ohci_device_intr_methods;
			ival = pipe->interval;
			if (ival == USBD_DEFAULT_INTERVAL)
				ival = ed->bInterval;
			return (ohci_device_setintr(sc, opipe, ival));
		case UE_ISOCHRONOUS:
			pipe->methods = &ohci_device_isoc_methods;
			return (ohci_setup_isoc(pipe));
		case UE_BULK:
			pipe->methods = &ohci_device_bulk_methods;
			s = splusb();
			ohci_add_ed(sed, sc->sc_bulk_head);
			splx(s);
			break;
		}
	}
	return (USBD_NORMAL_COMPLETION);

 bad:
	if (std != NULL)
		ohci_free_std(sc, std);
 bad1:
	if (sed != NULL)
		ohci_free_sed(sc, sed);
 bad0:
	return (USBD_NOMEM);

}

/*
 * Work around the half configured control (default) pipe when setting
 * the address of a device.
 *
 * Because a single ED is setup per endpoint in ohci_open(), and the
 * control pipe is configured before we could have set the address
 * of the device or read the wMaxPacketSize of the endpoint, we have
 * to re-open the pipe twice here.
 */
int
ohci_setaddr(struct usbd_device *dev, int addr)
{
	/* Root Hub */
	if (dev->depth == 0)
		return (0);

	/* Re-establish the default pipe with the new max packet size. */
	ohci_device_ctrl_close(dev->default_pipe);
	if (ohci_open(dev->default_pipe))
		return (EINVAL);

	if (usbd_set_address(dev, addr))
		return (1);

	dev->address = addr;

	/* Re-establish the default pipe with the new address. */
	ohci_device_ctrl_close(dev->default_pipe);
	if (ohci_open(dev->default_pipe))
		return (EINVAL);

	return (0);
}

/*
 * Close a regular pipe.
 * Assumes that there are no pending transactions.
 */
void
ohci_close_pipe(struct usbd_pipe *pipe, struct ohci_soft_ed *head)
{
	struct ohci_pipe *opipe = (struct ohci_pipe *)pipe;
	struct ohci_softc *sc = (struct ohci_softc *)pipe->device->bus;
	struct ohci_soft_ed *sed = opipe->sed;
	int s;

	s = splusb();
#ifdef DIAGNOSTIC
	sed->ed.ed_flags |= htole32(OHCI_ED_SKIP);
	if ((letoh32(sed->ed.ed_tailp) & OHCI_HEADMASK) !=
	    (letoh32(sed->ed.ed_headp) & OHCI_HEADMASK)) {
		struct ohci_soft_td *std;
		std = ohci_hash_find_td(sc, letoh32(sed->ed.ed_headp));
		printf("ohci_close_pipe: pipe not empty sed=%p hd=0x%x "
		       "tl=0x%x pipe=%p, std=%p\n", sed,
		       (int)letoh32(sed->ed.ed_headp),
		       (int)letoh32(sed->ed.ed_tailp),
		       pipe, std);
#ifdef OHCI_DEBUG
		ohci_dump_ed(sed);
		if (std)
			ohci_dump_td(std);
#endif
		usb_delay_ms(&sc->sc_bus, 2);
		if ((letoh32(sed->ed.ed_tailp) & OHCI_HEADMASK) !=
		    (letoh32(sed->ed.ed_headp) & OHCI_HEADMASK))
			printf("ohci_close_pipe: pipe still not empty\n");
	}
#endif
	ohci_rem_ed(sed, head);
	/* Make sure the host controller is not touching this ED */
	usb_delay_ms(&sc->sc_bus, 1);
	splx(s);
	pipe->endpoint->savedtoggle =
	    (letoh32(sed->ed.ed_headp) & OHCI_TOGGLECARRY) ? 1 : 0;
	ohci_free_sed(sc, opipe->sed);
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
ohci_abort_xfer(struct usbd_xfer *xfer, usbd_status status)
{
	struct ohci_pipe *opipe = (struct ohci_pipe *)xfer->pipe;
	struct ohci_softc *sc = (struct ohci_softc *)xfer->device->bus;
	struct ohci_soft_ed *sed = opipe->sed;
	struct ohci_soft_td *p, *n;
	ohci_physaddr_t headp;
	int s, hit;

	DPRINTF(("ohci_abort_xfer: xfer=%p pipe=%p sed=%p\n", xfer, opipe,
		 sed));

	if (sc->sc_bus.dying) {
		/* If we're dying, just do the software part. */
		s = splusb();
		xfer->status = status;	/* make software ignore it */
		timeout_del(&xfer->timeout_handle);
		usb_rem_task(xfer->device, &xfer->abort_task);
		usb_transfer_complete(xfer);
		splx(s);
		return;
	}

	if (xfer->device->bus->intr_context || !curproc)
		panic("ohci_abort_xfer: not in process context");

	/*
	 * Step 1: Make interrupt routine and hardware ignore xfer.
	 */
	s = splusb();
	xfer->status = status;	/* make software ignore it */
	timeout_del(&xfer->timeout_handle);
	usb_rem_task(xfer->device, &xfer->abort_task);
	splx(s);
	DPRINTFN(1,("ohci_abort_xfer: stop ed=%p\n", sed));
	sed->ed.ed_flags |= htole32(OHCI_ED_SKIP); /* force hardware skip */

	/*
	 * Step 2: Wait until we know hardware has finished any possible
	 * use of the xfer.  Also make sure the soft interrupt routine
	 * has run.
	 */
	usb_delay_ms(xfer->device->bus, 20); /* Hardware finishes in 1ms */
	s = splusb();
	sc->sc_softwake = 1;
	usb_schedsoftintr(&sc->sc_bus);
	tsleep_nsec(&sc->sc_softwake, PZERO, "ohciab", INFSLP);
	splx(s);

	/*
	 * Step 3: Remove any vestiges of the xfer from the hardware.
	 * The complication here is that the hardware may have executed
	 * beyond the xfer we're trying to abort.  So as we're scanning
	 * the TDs of this xfer we check if the hardware points to
	 * any of them.
	 */
	s = splusb();		/* XXX why? */
	p = xfer->hcpriv;
#ifdef DIAGNOSTIC
	if (p == NULL) {
		splx(s);
		printf("ohci_abort_xfer: hcpriv is NULL\n");
		return;
	}
#endif
#ifdef OHCI_DEBUG
	if (ohcidebug > 1) {
		DPRINTF(("ohci_abort_xfer: sed=\n"));
		ohci_dump_ed(sed);
		ohci_dump_tds(p);
	}
#endif
	headp = letoh32(sed->ed.ed_headp) & OHCI_HEADMASK;
	hit = 0;
	for (; p->xfer == xfer; p = n) {
		hit |= headp == p->physaddr;
		n = p->nexttd;
		if (OHCI_TD_GET_CC(letoh32(p->td.td_flags)) ==
		    OHCI_CC_NOT_ACCESSED)
			ohci_free_std(sc, p);
	}
	/* Zap headp register if hardware pointed inside the xfer. */
	if (hit) {
		DPRINTFN(1,("ohci_abort_xfer: set hd=0x%08x, tl=0x%08x\n",
			    (int)p->physaddr, (int)letoh32(sed->ed.ed_tailp)));
		sed->ed.ed_headp = htole32(p->physaddr); /* unlink TDs */
	} else {
		DPRINTFN(1,("ohci_abort_xfer: no hit\n"));
	}

	/*
	 * Step 4: Turn on hardware again.
	 */
	sed->ed.ed_flags &= htole32(~OHCI_ED_SKIP); /* remove hardware skip */

	/*
	 * Step 5: Execute callback.
	 */
	usb_transfer_complete(xfer);

	splx(s);
}

/*
 * Data structures and routines to emulate the root hub.
 */
const usb_device_descriptor_t ohci_devd = {
	USB_DEVICE_DESCRIPTOR_SIZE,
	UDESC_DEVICE,		/* type */
	{0x00, 0x01},		/* USB version */
	UDCLASS_HUB,		/* class */
	UDSUBCLASS_HUB,		/* subclass */
	UDPROTO_FSHUB,
	64,			/* max packet */
	{0},{0},{0x00,0x01},	/* device id */
	1,2,0,			/* string indices */
	1			/* # of configurations */
};

const usb_config_descriptor_t ohci_confd = {
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

const usb_interface_descriptor_t ohci_ifcd = {
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

const usb_endpoint_descriptor_t ohci_endpd = {
	USB_ENDPOINT_DESCRIPTOR_SIZE,
	UDESC_ENDPOINT,
	UE_DIR_IN | OHCI_INTR_ENDPT,
	UE_INTERRUPT,
	{8, 0},			/* max packet */
	255
};

const usb_hub_descriptor_t ohci_hubd = {
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
ohci_root_ctrl_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	/* Insert last in queue. */
	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	/* Pipe isn't running, start first */
	return (ohci_root_ctrl_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
ohci_root_ctrl_start(struct usbd_xfer *xfer)
{
	struct ohci_softc *sc = (struct ohci_softc *)xfer->device->bus;
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

	DPRINTFN(4,("ohci_root_ctrl_control type=0x%02x request=%02x\n",
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
		DPRINTFN(8,("ohci_root_ctrl_control wValue=0x%04x\n", value));
		switch(value >> 8) {
		case UDESC_DEVICE:
			if ((value & 0xff) != 0) {
				err = USBD_IOERROR;
				goto ret;
			}
			devd = ohci_devd;
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
			memcpy(buf, &ohci_confd, l);
			buf = (char *)buf + l;
			len -= l;
			l = min(len, USB_INTERFACE_DESCRIPTOR_SIZE);
			totlen += l;
			memcpy(buf, &ohci_ifcd, l);
			buf = (char *)buf + l;
			len -= l;
			l = min(len, USB_ENDPOINT_DESCRIPTOR_SIZE);
			totlen += l;
			memcpy(buf, &ohci_endpd, l);
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
				totlen = usbd_str(buf, len, "OHCI root hub");
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
		DPRINTFN(8, ("ohci_root_ctrl_control: UR_CLEAR_PORT_FEATURE "
			     "port=%d feature=%d\n",
			     index, value));
		if (index < 1 || index > sc->sc_noport) {
			err = USBD_IOERROR;
			goto ret;
		}
		port = OHCI_RH_PORT_STATUS(index);
		switch(value) {
		case UHF_PORT_ENABLE:
			OWRITE4(sc, port, UPS_CURRENT_CONNECT_STATUS);
			break;
		case UHF_PORT_SUSPEND:
			OWRITE4(sc, port, UPS_OVERCURRENT_INDICATOR);
			break;
		case UHF_PORT_POWER:
			/* Yes, writing to the LOW_SPEED bit clears power. */
			OWRITE4(sc, port, UPS_LOW_SPEED);
			break;
		case UHF_C_PORT_CONNECTION:
			OWRITE4(sc, port, UPS_C_CONNECT_STATUS << 16);
			break;
		case UHF_C_PORT_ENABLE:
			OWRITE4(sc, port, UPS_C_PORT_ENABLED << 16);
			break;
		case UHF_C_PORT_SUSPEND:
			OWRITE4(sc, port, UPS_C_SUSPEND << 16);
			break;
		case UHF_C_PORT_OVER_CURRENT:
			OWRITE4(sc, port, UPS_C_OVERCURRENT_INDICATOR << 16);
			break;
		case UHF_C_PORT_RESET:
			OWRITE4(sc, port, UPS_C_PORT_RESET << 16);
			break;
		default:
			err = USBD_IOERROR;
			goto ret;
		}
		switch(value) {
		case UHF_C_PORT_CONNECTION:
		case UHF_C_PORT_ENABLE:
		case UHF_C_PORT_SUSPEND:
		case UHF_C_PORT_OVER_CURRENT:
		case UHF_C_PORT_RESET:
			/* Enable RHSC interrupt if condition is cleared. */
			if ((OREAD4(sc, port) >> 16) == 0)
				ohci_rhsc_able(sc, 1);
			break;
		default:
			break;
		}
		break;
	case C(UR_GET_DESCRIPTOR, UT_READ_CLASS_DEVICE):
		if ((value & 0xff) != 0) {
			err = USBD_IOERROR;
			goto ret;
		}
		v = OREAD4(sc, OHCI_RH_DESCRIPTOR_A);
		hubd = ohci_hubd;
		hubd.bNbrPorts = sc->sc_noport;
		USETW(hubd.wHubCharacteristics,
		      (v & OHCI_NPS ? UHD_PWR_NO_SWITCH :
		       v & OHCI_PSM ? UHD_PWR_GANGED : UHD_PWR_INDIVIDUAL)
		      /* XXX overcurrent */
		      );
		hubd.bPwrOn2PwrGood = OHCI_GET_POTPGT(v);
		v = OREAD4(sc, OHCI_RH_DESCRIPTOR_B);
		for (i = 0, l = sc->sc_noport; l > 0; i++, l -= 8, v >>= 8)
			hubd.DeviceRemovable[i++] = (u_int8_t)v;
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
		DPRINTFN(8,("ohci_root_ctrl_transfer: get port status i=%d\n",
			    index));
		if (index < 1 || index > sc->sc_noport) {
			err = USBD_IOERROR;
			goto ret;
		}
		if (len != 4) {
			err = USBD_IOERROR;
			goto ret;
		}
		v = OREAD4(sc, OHCI_RH_PORT_STATUS(index));
		DPRINTFN(8,("ohci_root_ctrl_transfer: port status=0x%04x\n",
			    v));
		USETW(ps.wPortStatus, v);
		USETW(ps.wPortChange, v >> 16);
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
		if (index < 1 || index > sc->sc_noport) {
			err = USBD_IOERROR;
			goto ret;
		}
		port = OHCI_RH_PORT_STATUS(index);
		switch(value) {
		case UHF_PORT_ENABLE:
			OWRITE4(sc, port, UPS_PORT_ENABLED);
			break;
		case UHF_PORT_SUSPEND:
			OWRITE4(sc, port, UPS_SUSPEND);
			break;
		case UHF_PORT_RESET:
			DPRINTFN(5,("ohci_root_ctrl_transfer: reset port %d\n",
				    index));
			OWRITE4(sc, port, UPS_RESET);
			for (i = 0; i < 5; i++) {
				usb_delay_ms(&sc->sc_bus,
					     USB_PORT_ROOT_RESET_DELAY);
				if (sc->sc_bus.dying) {
					err = USBD_IOERROR;
					goto ret;
				}
				if ((OREAD4(sc, port) & UPS_RESET) == 0)
					break;
			}
			DPRINTFN(8,("ohci port %d reset, status = 0x%04x\n",
				    index, OREAD4(sc, port)));
			break;
		case UHF_PORT_POWER:
			DPRINTFN(2,("ohci_root_ctrl_transfer: set port power "
				    "%d\n", index));
			OWRITE4(sc, port, UPS_PORT_POWER);
			break;
		case UHF_PORT_DISOWN_TO_1_1:
			/* accept, but do nothing */
			break;
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
ohci_root_ctrl_abort(struct usbd_xfer *xfer)
{
	/* Nothing to do, all transfers are synchronous. */
}

/* Close the root pipe. */
void
ohci_root_ctrl_close(struct usbd_pipe *pipe)
{
	DPRINTF(("ohci_root_ctrl_close\n"));
	/* Nothing to do. */
}

usbd_status
ohci_root_intr_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	/* Insert last in queue. */
	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	/* Pipe isn't running, start first */
	return (ohci_root_intr_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
ohci_root_intr_start(struct usbd_xfer *xfer)
{
	struct ohci_softc *sc = (struct ohci_softc *)xfer->device->bus;

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	sc->sc_intrxfer = xfer;

	return (USBD_IN_PROGRESS);
}

void
ohci_root_intr_abort(struct usbd_xfer *xfer)
{
	struct ohci_softc *sc = (struct ohci_softc *)xfer->device->bus;
	int s;

	sc->sc_intrxfer = NULL;

	xfer->status = USBD_CANCELLED;
	s = splusb();
	usb_transfer_complete(xfer);
	splx(s);
}

void
ohci_root_intr_close(struct usbd_pipe *pipe)
{
}

usbd_status
ohci_device_ctrl_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	/* Insert last in queue. */
	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	/* Pipe isn't running, start first */
	return (ohci_device_ctrl_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
ohci_device_ctrl_start(struct usbd_xfer *xfer)
{
	struct ohci_softc *sc = (struct ohci_softc *)xfer->device->bus;
	usbd_status err;

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

#ifdef DIAGNOSTIC
	if (!(xfer->rqflags & URQ_REQUEST)) {
		/* XXX panic */
		printf("ohci_device_ctrl_transfer: not a request\n");
		return (USBD_INVAL);
	}
#endif

	err = ohci_device_request(xfer);
	if (err)
		return (err);

	return (USBD_IN_PROGRESS);
}

/* Abort a device control request. */
void
ohci_device_ctrl_abort(struct usbd_xfer *xfer)
{
	DPRINTF(("ohci_device_ctrl_abort: xfer=%p\n", xfer));
	ohci_abort_xfer(xfer, USBD_CANCELLED);
}

/* Close a device control pipe. */
void
ohci_device_ctrl_close(struct usbd_pipe *pipe)
{
	struct ohci_pipe *opipe = (struct ohci_pipe *)pipe;
	struct ohci_softc *sc = (struct ohci_softc *)pipe->device->bus;

	DPRINTF(("ohci_device_ctrl_close: pipe=%p\n", pipe));
	ohci_close_pipe(pipe, sc->sc_ctrl_head);
	ohci_free_std(sc, opipe->tail.td);
}

/************************/

void
ohci_device_clear_toggle(struct usbd_pipe *pipe)
{
	struct ohci_pipe *opipe = (struct ohci_pipe *)pipe;

	opipe->sed->ed.ed_headp &= htole32(~OHCI_TOGGLECARRY);
}

usbd_status
ohci_device_bulk_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	/* Insert last in queue. */
	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	/* Pipe isn't running, start first */
	return (ohci_device_bulk_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
ohci_device_bulk_start(struct usbd_xfer *xfer)
{
	struct ohci_softc *sc = (struct ohci_softc *)xfer->device->bus;
	struct ohci_pipe *opipe = (struct ohci_pipe *)xfer->pipe;
	struct ohci_soft_td *data, *tail, *tdp;
	struct ohci_soft_ed *sed;
	u_int len;
	int s, endpt;
	usbd_status err;

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

#ifdef DIAGNOSTIC
	if (xfer->rqflags & URQ_REQUEST) {
		/* XXX panic */
		printf("ohci_device_bulk_start: a request\n");
		return (USBD_INVAL);
	}
#endif

	len = xfer->length;
	endpt = xfer->pipe->endpoint->edesc->bEndpointAddress;
	sed = opipe->sed;

	DPRINTFN(4,("ohci_device_bulk_start: xfer=%p len=%u "
		    "flags=%d endpt=%d\n", xfer, len, xfer->flags, endpt));

	/* Update device address */
	sed->ed.ed_flags = htole32(
		(letoh32(sed->ed.ed_flags) & ~OHCI_ED_ADDRMASK) |
		OHCI_ED_SET_FA(xfer->device->address));

	/* Allocate a chain of new TDs (including a new tail). */
	data = opipe->tail.td;
	err = ohci_alloc_std_chain(sc, len, xfer, data, &tail);
	/* We want interrupt at the end of the transfer. */
	tail->td.td_flags &= htole32(~OHCI_TD_INTR_MASK);
	tail->td.td_flags |= htole32(OHCI_TD_SET_DI(1));
	tail->flags |= OHCI_CALL_DONE;
	tail = tail->nexttd;	/* point at sentinel */
	if (err)
		return (err);

	tail->xfer = NULL;
	xfer->hcpriv = data;

	DPRINTFN(4,("ohci_device_bulk_start: ed_flags=0x%08x td_flags=0x%08x "
		    "td_cbp=0x%08x td_be=0x%08x\n",
		    (int)letoh32(sed->ed.ed_flags),
		    (int)letoh32(data->td.td_flags),
		    (int)letoh32(data->td.td_cbp),
		    (int)letoh32(data->td.td_be)));

#ifdef OHCI_DEBUG
	if (ohcidebug > 5) {
		ohci_dump_ed(sed);
		ohci_dump_tds(data);
	}
#endif

	/* Insert ED in schedule */
	s = splusb();
	for (tdp = data; tdp != tail; tdp = tdp->nexttd) {
		tdp->xfer = xfer;
	}
	sed->ed.ed_tailp = htole32(tail->physaddr);
	opipe->tail.td = tail;
	sed->ed.ed_flags &= htole32(~OHCI_ED_SKIP);
	OWRITE4(sc, OHCI_COMMAND_STATUS, OHCI_BLF);
	if (xfer->timeout && !sc->sc_bus.use_polling) {
                timeout_del(&xfer->timeout_handle);
                timeout_set(&xfer->timeout_handle, ohci_timeout, xfer);
                timeout_add_msec(&xfer->timeout_handle, xfer->timeout);
	}

#if 0
/* This goes wrong if we are too slow. */
	if (ohcidebug > 10) {
		delay(10000);
		DPRINTF(("ohci_device_intr_transfer: status=%x\n",
			 OREAD4(sc, OHCI_COMMAND_STATUS)));
		ohci_dump_ed(sed);
		ohci_dump_tds(data);
	}
#endif

	splx(s);

	return (USBD_IN_PROGRESS);
}

void
ohci_device_bulk_abort(struct usbd_xfer *xfer)
{
	DPRINTF(("ohci_device_bulk_abort: xfer=%p\n", xfer));
	ohci_abort_xfer(xfer, USBD_CANCELLED);
}

/*
 * Close a device bulk pipe.
 */
void
ohci_device_bulk_close(struct usbd_pipe *pipe)
{
	struct ohci_pipe *opipe = (struct ohci_pipe *)pipe;
	struct ohci_softc *sc = (struct ohci_softc *)pipe->device->bus;

	DPRINTF(("ohci_device_bulk_close: pipe=%p\n", pipe));
	ohci_close_pipe(pipe, sc->sc_bulk_head);
	ohci_free_std(sc, opipe->tail.td);
}

/************************/

usbd_status
ohci_device_intr_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	/* Insert last in queue. */
	err = usb_insert_transfer(xfer);
	if (err)
		return (err);

	/* Pipe isn't running, start first */
	return (ohci_device_intr_start(SIMPLEQ_FIRST(&xfer->pipe->queue)));
}

usbd_status
ohci_device_intr_start(struct usbd_xfer *xfer)
{
	struct ohci_softc *sc = (struct ohci_softc *)xfer->device->bus;
	struct ohci_pipe *opipe = (struct ohci_pipe *)xfer->pipe;
	struct ohci_soft_ed *sed = opipe->sed;
	struct ohci_soft_td *data, *tail;
	int s, len, endpt;

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

	DPRINTFN(3, ("ohci_device_intr_transfer: xfer=%p len=%u "
		     "flags=%d priv=%p\n",
		     xfer, xfer->length, xfer->flags, xfer->priv));

#ifdef DIAGNOSTIC
	if (xfer->rqflags & URQ_REQUEST)
		panic("ohci_device_intr_transfer: a request");
#endif

	usb_syncmem(&xfer->dmabuf, 0, xfer->length,
	    usbd_xfer_isread(xfer) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	len = xfer->length;
	endpt = xfer->pipe->endpoint->edesc->bEndpointAddress;

	data = opipe->tail.td;
	tail = ohci_alloc_std(sc);
	if (tail == NULL)
		return (USBD_NOMEM);
	tail->xfer = NULL;

	data->td.td_flags = htole32(
		(usbd_xfer_isread(xfer) ? OHCI_TD_IN : OHCI_TD_OUT) |
		OHCI_TD_NOCC |
		OHCI_TD_SET_DI(1) | OHCI_TD_TOGGLE_CARRY);
	if (xfer->flags & USBD_SHORT_XFER_OK)
		data->td.td_flags |= htole32(OHCI_TD_R);
	data->td.td_cbp = htole32(DMAADDR(&xfer->dmabuf, 0));
	data->nexttd = tail;
	data->td.td_nexttd = htole32(tail->physaddr);
	data->td.td_be = htole32(letoh32(data->td.td_cbp) + len - 1);
	data->len = len;
	data->xfer = xfer;
	data->flags = OHCI_CALL_DONE | OHCI_ADD_LEN;
	xfer->hcpriv = data;

#ifdef OHCI_DEBUG
	if (ohcidebug > 5) {
		DPRINTF(("ohci_device_intr_transfer:\n"));
		ohci_dump_ed(sed);
		ohci_dump_tds(data);
	}
#endif

	/* Insert ED in schedule */
	s = splusb();
	sed->ed.ed_tailp = htole32(tail->physaddr);
	opipe->tail.td = tail;
	sed->ed.ed_flags &= htole32(~OHCI_ED_SKIP);

#if 0
/*
 * This goes horribly wrong, printing thousands of descriptors,
 * because false references are followed due to the fact that the
 * TD is gone.
 */
	if (ohcidebug > 5) {
		usb_delay_ms(&sc->sc_bus, 5);
		DPRINTF(("ohci_device_intr_transfer: status=%x\n",
			 OREAD4(sc, OHCI_COMMAND_STATUS)));
		ohci_dump_ed(sed);
		ohci_dump_tds(data);
	}
#endif
	splx(s);

	return (USBD_IN_PROGRESS);
}

void
ohci_device_intr_abort(struct usbd_xfer *xfer)
{
	KASSERT(!xfer->pipe->repeat || xfer->pipe->intrxfer == xfer);

	ohci_abort_xfer(xfer, USBD_CANCELLED);
}

/* Close a device interrupt pipe. */
void
ohci_device_intr_close(struct usbd_pipe *pipe)
{
	struct ohci_pipe *opipe = (struct ohci_pipe *)pipe;
	struct ohci_softc *sc = (struct ohci_softc *)pipe->device->bus;
	int nslots = opipe->u.intr.nslots;
	int pos = opipe->u.intr.pos;
	int j;
	struct ohci_soft_ed *p, *sed = opipe->sed;
	int s;

	DPRINTFN(1,("ohci_device_intr_close: pipe=%p nslots=%d pos=%d\n",
		    pipe, nslots, pos));
	s = splusb();
	sed->ed.ed_flags |= htole32(OHCI_ED_SKIP);
	if ((letoh32(sed->ed.ed_tailp) & OHCI_HEADMASK) !=
	    (letoh32(sed->ed.ed_headp) & OHCI_HEADMASK))
		usb_delay_ms(&sc->sc_bus, 2);

	for (p = sc->sc_eds[pos]; p && p->next != sed; p = p->next)
		;
#ifdef DIAGNOSTIC
	if (p == NULL)
		panic("ohci_device_intr_close: ED not found");
#endif
	p->next = sed->next;
	p->ed.ed_nexted = sed->ed.ed_nexted;
	splx(s);

	for (j = 0; j < nslots; j++)
		--sc->sc_bws[(pos * nslots + j) % OHCI_NO_INTRS];

	ohci_free_std(sc, opipe->tail.td);
	ohci_free_sed(sc, opipe->sed);
}

usbd_status
ohci_device_setintr(struct ohci_softc *sc, struct ohci_pipe *opipe, int ival)
{
	int i, j, s, best;
	u_int npoll, slow, shigh, nslots;
	u_int bestbw, bw;
	struct ohci_soft_ed *hsed, *sed = opipe->sed;

	DPRINTFN(2, ("ohci_setintr: pipe=%p\n", opipe));
	if (ival == 0) {
		printf("ohci_setintr: 0 interval\n");
		return (USBD_INVAL);
	}

	npoll = OHCI_NO_INTRS;
	while (npoll > ival)
		npoll /= 2;
	DPRINTFN(2, ("ohci_setintr: ival=%d npoll=%d\n", ival, npoll));

	/*
	 * We now know which level in the tree the ED must go into.
	 * Figure out which slot has most bandwidth left over.
	 * Slots to examine:
	 * npoll
	 * 1	0
	 * 2	1 2
	 * 4	3 4 5 6
	 * 8	7 8 9 10 11 12 13 14
	 * N    (N-1) .. (N-1+N-1)
	 */
	slow = npoll-1;
	shigh = slow + npoll;
	nslots = OHCI_NO_INTRS / npoll;
	for (best = i = slow, bestbw = ~0; i < shigh; i++) {
		bw = 0;
		for (j = 0; j < nslots; j++)
			bw += sc->sc_bws[(i * nslots + j) % OHCI_NO_INTRS];
		if (bw < bestbw) {
			best = i;
			bestbw = bw;
		}
	}
	DPRINTFN(2, ("ohci_setintr: best=%d(%d..%d) bestbw=%d\n",
		     best, slow, shigh, bestbw));

	s = splusb();
	hsed = sc->sc_eds[best];
	sed->next = hsed->next;
	sed->ed.ed_nexted = hsed->ed.ed_nexted;
	hsed->next = sed;
	hsed->ed.ed_nexted = htole32(sed->physaddr);
	splx(s);

	for (j = 0; j < nslots; j++)
		++sc->sc_bws[(best * nslots + j) % OHCI_NO_INTRS];
	opipe->u.intr.nslots = nslots;
	opipe->u.intr.pos = best;

	DPRINTFN(5, ("ohci_setintr: returns %p\n", opipe));
	return (USBD_NORMAL_COMPLETION);
}

/***********************/

usbd_status
ohci_device_isoc_transfer(struct usbd_xfer *xfer)
{
	usbd_status err;

	DPRINTFN(5,("ohci_device_isoc_transfer: xfer=%p\n", xfer));

	/* Put it on our queue, */
	err = usb_insert_transfer(xfer);

	/* bail out on error, */
	if (err && err != USBD_IN_PROGRESS)
		return (err);

	/* XXX should check inuse here */

	/* insert into schedule, */
	ohci_device_isoc_enter(xfer);

	/* and start if the pipe wasn't running */
	if (!err)
		ohci_device_isoc_start(SIMPLEQ_FIRST(&xfer->pipe->queue));

	return (err);
}

void
ohci_device_isoc_enter(struct usbd_xfer *xfer)
{
	struct ohci_softc *sc = (struct ohci_softc *)xfer->device->bus;
	struct ohci_pipe *opipe = (struct ohci_pipe *)xfer->pipe;
	struct ohci_soft_ed *sed = opipe->sed;
	struct iso *iso = &opipe->u.iso;
	struct ohci_soft_itd *sitd, *nsitd;
	ohci_physaddr_t buf, offs, noffs, bp0;
	int i, ncur, nframes;
	int s;

	DPRINTFN(1,("ohci_device_isoc_enter: used=%d next=%d xfer=%p "
		    "nframes=%d\n",
		    iso->inuse, iso->next, xfer, xfer->nframes));

	if (sc->sc_bus.dying)
		return;

	usb_syncmem(&xfer->dmabuf, 0, xfer->length,
	    usbd_xfer_isread(xfer) ?
	    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	if (iso->next == -1) {
		/* Not in use yet, schedule it a few frames ahead. */
		iso->next = letoh32(sc->sc_hcca->hcca_frame_number) + 5;
		DPRINTFN(2,("ohci_device_isoc_enter: start next=%d\n",
			    iso->next));
	}

	sitd = opipe->tail.itd;
	buf = DMAADDR(&xfer->dmabuf, 0);
	bp0 = OHCI_PAGE(buf);
	offs = OHCI_PAGE_OFFSET(buf);
	nframes = xfer->nframes;
	xfer->hcpriv = sitd;
	for (i = ncur = 0; i < nframes; i++, ncur++) {
		noffs = offs + xfer->frlengths[i];
		if (ncur == OHCI_ITD_NOFFSET ||	/* all offsets used */
		    OHCI_PAGE(buf + noffs) > bp0 + OHCI_PAGE_SIZE) { /* too many page crossings */

			/* Allocate next ITD */
			nsitd = ohci_alloc_sitd(sc);
			if (nsitd == NULL) {
				/* XXX what now? */
				printf("%s: isoc TD alloc failed\n",
				       sc->sc_bus.bdev.dv_xname);
				return;
			}

			/* Fill current ITD */
			sitd->itd.itd_flags = htole32(
				OHCI_ITD_NOCC |
				OHCI_ITD_SET_SF(iso->next) |
				OHCI_ITD_SET_DI(6) | /* delay intr a little */
				OHCI_ITD_SET_FC(ncur));
			sitd->itd.itd_bp0 = htole32(bp0);
			sitd->nextitd = nsitd;
			sitd->itd.itd_nextitd = htole32(nsitd->physaddr);
			sitd->itd.itd_be = htole32(bp0 + offs - 1);
			sitd->xfer = xfer;
			sitd->flags = 0;

			sitd = nsitd;
			iso->next = iso->next + ncur;
			bp0 = OHCI_PAGE(buf + offs);
			ncur = 0;
		}
		sitd->itd.itd_offset[ncur] = htole16(OHCI_ITD_MK_OFFS(offs));
		offs = noffs;
	}
	nsitd = ohci_alloc_sitd(sc);
	if (nsitd == NULL) {
		/* XXX what now? */
		printf("%s: isoc TD alloc failed\n",
		       sc->sc_bus.bdev.dv_xname);
		return;
	}
	/* Fixup last used ITD */
	sitd->itd.itd_flags = htole32(
		OHCI_ITD_NOCC |
		OHCI_ITD_SET_SF(iso->next) |
		OHCI_ITD_SET_DI(0) |
		OHCI_ITD_SET_FC(ncur));
	sitd->itd.itd_bp0 = htole32(bp0);
	sitd->nextitd = nsitd;
	sitd->itd.itd_nextitd = htole32(nsitd->physaddr);
	sitd->itd.itd_be = htole32(bp0 + offs - 1);
	sitd->xfer = xfer;
	sitd->flags = OHCI_CALL_DONE;

	iso->next = iso->next + ncur;
	iso->inuse += nframes;

	xfer->actlen = offs;	/* XXX pretend we did it all */

	xfer->status = USBD_IN_PROGRESS;

#ifdef OHCI_DEBUG
	if (ohcidebug > 5) {
		DPRINTF(("ohci_device_isoc_enter: frame=%d\n",
			 letoh32(sc->sc_hcca->hcca_frame_number)));
		ohci_dump_itds(xfer->hcpriv);
		ohci_dump_ed(sed);
	}
#endif

	s = splusb();
	sed->ed.ed_tailp = htole32(nsitd->physaddr);
	opipe->tail.itd = nsitd;
	sed->ed.ed_flags &= htole32(~OHCI_ED_SKIP);
	splx(s);

#ifdef OHCI_DEBUG
	if (ohcidebug > 5) {
		delay(150000);
		DPRINTF(("ohci_device_isoc_enter: after frame=%d\n",
			 letoh32(sc->sc_hcca->hcca_frame_number)));
		ohci_dump_itds(xfer->hcpriv);
		ohci_dump_ed(sed);
	}
#endif
}

usbd_status
ohci_device_isoc_start(struct usbd_xfer *xfer)
{
	struct ohci_softc *sc = (struct ohci_softc *)xfer->device->bus;

	DPRINTFN(5,("ohci_device_isoc_start: xfer=%p\n", xfer));

	if (sc->sc_bus.dying)
		return (USBD_IOERROR);

#ifdef DIAGNOSTIC
	if (xfer->status != USBD_IN_PROGRESS)
		printf("ohci_device_isoc_start: not in progress %p\n", xfer);
#endif

	return (USBD_IN_PROGRESS);
}

void
ohci_device_isoc_abort(struct usbd_xfer *xfer)
{
	struct ohci_softc *sc = (struct ohci_softc *)xfer->device->bus;
	struct ohci_pipe *opipe = (struct ohci_pipe *)xfer->pipe;
	struct ohci_soft_ed *sed;
	struct ohci_soft_itd *sitd;
	int s;

	s = splusb();

	DPRINTFN(1,("ohci_device_isoc_abort: xfer=%p\n", xfer));

	/* Transfer is already done. */
	if (xfer->status != USBD_NOT_STARTED &&
	    xfer->status != USBD_IN_PROGRESS) {
		splx(s);
		printf("ohci_device_isoc_abort: early return\n");
		return;
	}

	/* Give xfer the requested abort code. */
	xfer->status = USBD_CANCELLED;

	sed = opipe->sed;
	sed->ed.ed_flags |= htole32(OHCI_ED_SKIP); /* force hardware skip */

	sitd = xfer->hcpriv;
#ifdef DIAGNOSTIC
	if (sitd == NULL) {
		splx(s);
		printf("ohci_device_isoc_abort: hcpriv==0\n");
		return;
	}
#endif
	for (; sitd->xfer == xfer; sitd = sitd->nextitd) {
#ifdef DIAGNOSTIC
		DPRINTFN(1,("abort sets done sitd=%p\n", sitd));
		sitd->isdone = 1;
#endif
	}

	splx(s);

	usb_delay_ms(&sc->sc_bus, OHCI_ITD_NOFFSET);

	s = splusb();

	/* Run callback. */
	usb_transfer_complete(xfer);

	sed->ed.ed_headp = htole32(sitd->physaddr); /* unlink TDs */
	sed->ed.ed_flags &= htole32(~OHCI_ED_SKIP); /* remove hardware skip */

	splx(s);
}

void
ohci_device_isoc_done(struct usbd_xfer *xfer)
{
	DPRINTFN(1,("ohci_device_isoc_done: xfer=%p\n", xfer));
}

usbd_status
ohci_setup_isoc(struct usbd_pipe *pipe)
{
	struct ohci_pipe *opipe = (struct ohci_pipe *)pipe;
	struct ohci_softc *sc = (struct ohci_softc *)pipe->device->bus;
	struct iso *iso = &opipe->u.iso;
	int s;

	iso->next = -1;
	iso->inuse = 0;

	s = splusb();
	ohci_add_ed(opipe->sed, sc->sc_isoc_head);
	splx(s);

	return (USBD_NORMAL_COMPLETION);
}

void
ohci_device_isoc_close(struct usbd_pipe *pipe)
{
	struct ohci_pipe *opipe = (struct ohci_pipe *)pipe;
	struct ohci_softc *sc = (struct ohci_softc *)pipe->device->bus;

	DPRINTF(("ohci_device_isoc_close: pipe=%p\n", pipe));
	ohci_close_pipe(pipe, sc->sc_isoc_head);
#ifdef DIAGNOSTIC
	opipe->tail.itd->isdone = 1;
#endif
	ohci_free_sitd(sc, opipe->tail.itd);
}
