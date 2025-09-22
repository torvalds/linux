/*	$OpenBSD: ehcivar.h,v 1.37 2016/10/02 06:36:39 kettenis Exp $ */
/*	$NetBSD: ehcivar.h,v 1.19 2005/04/29 15:04:29 augustss Exp $	*/

/*
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net).
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

struct ehci_soft_qtd {
	struct ehci_qtd qtd;
	struct ehci_soft_qtd *nextqtd; /* mirrors nextqtd in TD */
	ehci_physaddr_t physaddr;
	struct usb_dma dma;             /* qTD's DMA infos */
	int offs;                       /* qTD's offset in struct usb_dma */
	LIST_ENTRY(ehci_soft_qtd) hnext;
	u_int16_t len;
};
#define EHCI_SQTD_SIZE ((sizeof (struct ehci_soft_qtd) + EHCI_QTD_ALIGN - 1) / EHCI_QTD_ALIGN * EHCI_QTD_ALIGN)
#define EHCI_SQTD_CHUNK (EHCI_PAGE_SIZE / EHCI_SQTD_SIZE)

struct ehci_soft_qh {
	struct ehci_qh qh;
	struct ehci_soft_qh *next;
	struct ehci_soft_qh *prev;
	struct ehci_soft_qtd *sqtd;
	ehci_physaddr_t physaddr;
	struct usb_dma dma;             /* QH's DMA infos */
	int offs;                       /* QH's offset in struct usb_dma */
	int islot;
};
#define EHCI_SQH_SIZE ((sizeof (struct ehci_soft_qh) + EHCI_QH_ALIGN - 1) / EHCI_QH_ALIGN * EHCI_QH_ALIGN)
#define EHCI_SQH_CHUNK (EHCI_PAGE_SIZE / EHCI_SQH_SIZE)

struct ehci_soft_itd {
	union {
		struct ehci_itd itd;
		struct ehci_sitd sitd;
	};
	union {
		struct {
			/* soft_itds links in a periodic frame*/
			struct ehci_soft_itd *next;
			struct ehci_soft_itd *prev;
		} frame_list;
		/* circular list of free itds */
		LIST_ENTRY(ehci_soft_itd) free_list;
	} u;
	struct ehci_soft_itd *xfer_next; /* Next soft_itd in xfer */
	ehci_physaddr_t physaddr;
	struct usb_dma dma;
	int offs;
	int slot;
	struct timeval t; /* store free time */
};
#define EHCI_ITD_SIZE ((sizeof(struct ehci_soft_itd) + EHCI_QH_ALIGN - 1) / EHCI_ITD_ALIGN * EHCI_ITD_ALIGN)
#define EHCI_ITD_CHUNK (EHCI_PAGE_SIZE / EHCI_ITD_SIZE)

struct ehci_xfer {
	struct usbd_xfer	xfer;
	TAILQ_ENTRY(ehci_xfer)	inext;		/* List of active xfers */
	union {
		struct {
			struct ehci_soft_qtd *start, *end;
		} sqtd;				/* Ctrl/Bulk/Interrupt TD */
		struct {
			struct ehci_soft_itd *start, *end;
		} itd;				/* Isochronous TD */
	} _TD;
#define sqtdstart	_TD.sqtd.start
#define sqtdend		_TD.sqtd.end
#define itdstart	_TD.itd.start
#define itdend		_TD.itd.end

	uint32_t		ehci_xfer_flags;
#ifdef DIAGNOSTIC
	int isdone;
#endif
};
#define EHCI_XFER_ABORTING	0x0001	/* xfer is aborting. */
#define EHCI_XFER_ABORTWAIT	0x0002	/* abort completion is being awaited. */

/* Information about an entry in the interrupt list. */
struct ehci_soft_islot {
	struct ehci_soft_qh *sqh;	/* Queue Head. */
};

#define EHCI_FRAMELIST_MAXCOUNT	1024
#define EHCI_IPOLLRATES		8 /* Poll rates (1ms, 2, 4, 8 .. 128) */
#define EHCI_INTRQHS		((1 << EHCI_IPOLLRATES) - 1)
#define EHCI_IQHIDX(lev, pos) \
	((((pos) & ((1 << (lev)) - 1)) | (1 << (lev))) - 1)
#define EHCI_ILEV_IVAL(lev)	(1 << (lev))


#define EHCI_HASH_SIZE 128
#define EHCI_COMPANION_MAX 8

#define EHCI_FREE_LIST_INTERVAL 100

struct ehci_softc {
	struct usbd_bus sc_bus;		/* base device */
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	bus_size_t sc_size;
	u_int sc_offs;			/* offset to operational regs */
	int sc_flags;			/* misc flags */
#define EHCIF_DROPPED_INTR_WORKAROUND	0x01
#define EHCIF_PCB_INTR			0x02
#define EHCIF_USBMODE			0x04

	char sc_vendor[16];		/* vendor string for root hub */
	int sc_id_vendor;		/* vendor ID for root hub */

	struct usb_dma sc_fldma;
	ehci_link_t *sc_flist;
	u_int sc_flsize;

	struct ehci_soft_islot sc_islots[EHCI_INTRQHS];

	/* jcmm - an array matching sc_flist, but with software pointers,
	 * not hardware address pointers
	 */
	struct ehci_soft_itd **sc_softitds;

	TAILQ_HEAD(, ehci_xfer) sc_intrhead;

	struct ehci_soft_qh *sc_freeqhs;
	struct ehci_soft_qtd *sc_freeqtds;
	LIST_HEAD(sc_freeitds, ehci_soft_itd) sc_freeitds;

	int sc_noport;
	u_int8_t sc_conf;		/* device configuration */
	struct usbd_xfer *sc_intrxfer;
	char sc_isreset;
	char sc_softwake;

	u_int32_t sc_eintrs;
	struct ehci_soft_qh *sc_async_head;

	struct rwlock sc_doorbell_lock;

	struct timeout sc_tmo_intrlist;
};

#define EREAD1(sc, a) bus_space_read_1((sc)->iot, (sc)->ioh, (a))
#define EREAD2(sc, a) bus_space_read_2((sc)->iot, (sc)->ioh, (a))
#define EREAD4(sc, a) bus_space_read_4((sc)->iot, (sc)->ioh, (a))
#define EWRITE1(sc, a, x) bus_space_write_1((sc)->iot, (sc)->ioh, (a), (x))
#define EWRITE2(sc, a, x) bus_space_write_2((sc)->iot, (sc)->ioh, (a), (x))
#define EWRITE4(sc, a, x) bus_space_write_4((sc)->iot, (sc)->ioh, (a), (x))
#define EOREAD1(sc, a) bus_space_read_1((sc)->iot, (sc)->ioh, (sc)->sc_offs+(a))
#define EOREAD2(sc, a) bus_space_read_2((sc)->iot, (sc)->ioh, (sc)->sc_offs+(a))
#define EOREAD4(sc, a) bus_space_read_4((sc)->iot, (sc)->ioh, (sc)->sc_offs+(a))
#define EOWRITE1(sc, a, x) bus_space_write_1((sc)->iot, (sc)->ioh, (sc)->sc_offs+(a), (x))
#define EOWRITE2(sc, a, x) bus_space_write_2((sc)->iot, (sc)->ioh, (sc)->sc_offs+(a), (x))
#define EOWRITE4(sc, a, x) bus_space_write_4((sc)->iot, (sc)->ioh, (sc)->sc_offs+(a), (x))

usbd_status	ehci_init(struct ehci_softc *);
int		ehci_intr(void *);
int		ehci_detach(struct device *, int);
int		ehci_activate(struct device *, int);
usbd_status	ehci_reset(struct ehci_softc *);

