/*	$OpenBSD: uhcivar.h,v 1.33 2014/05/18 17:10:27 mpi Exp $ */
/*	$NetBSD: uhcivar.h,v 1.36 2002/12/31 00:39:11 augustss Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/uhcivar.h,v 1.14 1999/11/17 22:33:42 n_hibma Exp $	*/

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

/*
 * To avoid having 1024 TDs for each isochronous transfer we introduce
 * a virtual frame list.  Every UHCI_VFRAMELIST_COUNT entries in the real
 * frame list points to a non-active TD.  These, in turn, form the
 * starts of the virtual frame list.  This also has the advantage that it
 * simplifies linking in/out of TDs/QHs in the schedule.
 * Furthermore, initially each of the inactive TDs point to an inactive
 * QH that forms the start of the interrupt traffic for that slot.
 * Each of these QHs point to the same QH that is the start of control
 * traffic.  This QH points at another QH which is the start of the
 * bulk traffic.
 *
 * UHCI_VFRAMELIST_COUNT should be a power of 2 and <= UHCI_FRAMELIST_COUNT.
 */
#define UHCI_VFRAMELIST_COUNT 128

struct uhci_soft_qh;
struct uhci_soft_td;

typedef union {
	struct uhci_soft_qh *sqh;
	struct uhci_soft_td *std;
} uhci_soft_td_qh_t;

/*
 * An interrupt info struct contains the information needed to
 * execute a requested routine when the controller generates an
 * interrupt.  Since we cannot know which transfer generated
 * the interrupt all structs are linked together so they can be
 * searched at interrupt time.
 */
struct uhci_xfer {
	struct usbd_xfer xfer;
	LIST_ENTRY(uhci_xfer) inext;
	struct uhci_soft_td *stdstart;
	struct uhci_soft_td *stdend;
	int curframe;
#ifdef DIAGNOSTIC
	int isdone;
#endif
};

/*
 * Extra information that we need for a TD.
 */
struct uhci_soft_td {
	struct uhci_td td;		/* The real TD, must be first */
	uhci_soft_td_qh_t link; 	/* soft version of the td_link field */
	uhci_physaddr_t physaddr;	/* TD's physical address. */
};
/*
 * Make the size such that it is a multiple of UHCI_TD_ALIGN.  This way
 * we can pack a number of soft TD together and have the real TD well
 * aligned.
 * NOTE: Minimum size is 32 bytes.
 */
#define UHCI_STD_SIZE ((sizeof (struct uhci_soft_td) + UHCI_TD_ALIGN - 1) / UHCI_TD_ALIGN * UHCI_TD_ALIGN)
#define UHCI_STD_CHUNK 128 /*(PAGE_SIZE / UHCI_TD_SIZE)*/

/*
 * Extra information that we need for a QH.
 */
struct uhci_soft_qh {
	struct uhci_qh qh;		/* The real QH, must be first */
	struct uhci_soft_qh *hlink;	/* soft version of qh_hlink */
	struct uhci_soft_td *elink;	/* soft version of qh_elink */
	uhci_physaddr_t physaddr;	/* QH's physical address. */
	int pos;			/* Timeslot position */
};
/* See comment about UHCI_STD_SIZE. */
#define UHCI_SQH_SIZE ((sizeof (struct uhci_soft_qh) + UHCI_QH_ALIGN - 1) / UHCI_QH_ALIGN * UHCI_QH_ALIGN)
#define UHCI_SQH_CHUNK 128 /*(PAGE_SIZE / UHCI_QH_SIZE)*/

/*
 * Information about an entry in the virtual frame list.
 */
struct uhci_vframe {
	struct uhci_soft_td *htd;	/* pointer to dummy TD */
	struct uhci_soft_td *etd;	/* pointer to last TD */
	struct uhci_soft_qh *hqh;	/* pointer to dummy QH */
	struct uhci_soft_qh *eqh;	/* pointer to last QH */
	u_int bandwidth;		/* max bandwidth used by this frame */
};

struct uhci_softc {
	struct usbd_bus sc_bus;		/* base device */
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	bus_size_t sc_size;

	uhci_physaddr_t *sc_pframes;
	struct usb_dma sc_dma;
	struct uhci_vframe sc_vframes[UHCI_VFRAMELIST_COUNT];

	struct uhci_soft_qh *sc_lctl_start; /* dummy QH for low speed control */
	struct uhci_soft_qh *sc_lctl_end; /* last control QH */
	struct uhci_soft_qh *sc_hctl_start;/* dummy QH for high speed control */
	struct uhci_soft_qh *sc_hctl_end; /* last control QH */
	struct uhci_soft_qh *sc_bulk_start; /* dummy QH for bulk */
	struct uhci_soft_qh *sc_bulk_end; /* last bulk transfer */
	struct uhci_soft_qh *sc_last_qh; /* dummy QH at the end */
	u_int32_t sc_loops;		/* number of QHs that wants looping */

	struct uhci_soft_td *sc_freetds; /* TD free list */
	struct uhci_soft_qh *sc_freeqhs; /* QH free list */

	u_int8_t sc_conf;		/* device configuration */

	u_int8_t sc_saved_sof;
	u_int16_t sc_saved_frnum;

	char sc_softwake;

	char sc_isreset;
	char sc_suspend;

	LIST_HEAD(, uhci_xfer) sc_intrhead;

	/* Info for the root hub interrupt "pipe". */
	struct usbd_xfer	*sc_intrxfer;
	struct timeout		 sc_root_intr;

	char sc_vendor[32];		/* vendor string for root hub */
	int sc_id_vendor;		/* vendor ID for root hub */
};

usbd_status	uhci_init(struct uhci_softc *);
usbd_status	uhci_run(struct uhci_softc *, int run);
int		uhci_intr(void *);
int		uhci_detach(struct device *, int);
int		uhci_activate(struct device *, int);
