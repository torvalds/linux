/*	$OpenBSD: ohcivar.h,v 1.39 2017/06/01 09:47:55 mpi Exp $ */
/*	$NetBSD: ohcivar.h,v 1.32 2003/02/22 05:24:17 tsutsui Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/ohcivar.h,v 1.13 1999/11/17 22:33:41 n_hibma Exp $	*/

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

struct ohci_soft_ed {
	struct ohci_ed ed;
	struct ohci_soft_ed *next;
	ohci_physaddr_t physaddr;
};
#define OHCI_SED_SIZE ((sizeof (struct ohci_soft_ed) + OHCI_ED_ALIGN - 1) / OHCI_ED_ALIGN * OHCI_ED_ALIGN)
#define OHCI_SED_CHUNK 128


struct ohci_soft_td {
	struct ohci_td td;
	struct ohci_soft_td *nexttd; /* mirrors nexttd in TD */
	struct ohci_soft_td *dnext; /* next in done list */
	ohci_physaddr_t physaddr;
	LIST_ENTRY(ohci_soft_td) hnext;
	struct usbd_xfer *xfer;
	u_int16_t len;
	u_int16_t flags;
#define OHCI_CALL_DONE	0x0001
#define OHCI_ADD_LEN	0x0002
};
#define OHCI_STD_SIZE ((sizeof (struct ohci_soft_td) + OHCI_TD_ALIGN - 1) / OHCI_TD_ALIGN * OHCI_TD_ALIGN)
#define OHCI_STD_CHUNK 128


struct ohci_soft_itd {
	struct ohci_itd itd;
	struct ohci_soft_itd *nextitd; /* mirrors nexttd in ITD */
	struct ohci_soft_itd *dnext; /* next in done list */
	ohci_physaddr_t physaddr;
	LIST_ENTRY(ohci_soft_itd) hnext;
	struct usbd_xfer *xfer;
	u_int16_t flags;
#ifdef DIAGNOSTIC
	char isdone;
#endif
};
#define OHCI_SITD_SIZE ((sizeof (struct ohci_soft_itd) + OHCI_ITD_ALIGN - 1) / OHCI_ITD_ALIGN * OHCI_ITD_ALIGN)
#define OHCI_SITD_CHUNK 64


#define OHCI_NO_EDS (2*OHCI_NO_INTRS-1)

#define OHCI_HASH_SIZE 128

struct ohci_softc {
	struct usbd_bus sc_bus;		/* base device */
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	bus_size_t sc_size;

	struct usb_dma sc_hccadma;
	struct ohci_hcca *sc_hcca;
	struct ohci_soft_ed *sc_eds[OHCI_NO_EDS];
	u_int sc_bws[OHCI_NO_INTRS];

	u_int32_t sc_eintrs;
	struct ohci_soft_ed *sc_isoc_head;
	struct ohci_soft_ed *sc_ctrl_head;
	struct ohci_soft_ed *sc_bulk_head;

	LIST_HEAD(, ohci_soft_td)  sc_hash_tds[OHCI_HASH_SIZE];
	LIST_HEAD(, ohci_soft_itd) sc_hash_itds[OHCI_HASH_SIZE];

	int sc_noport;
	u_int8_t sc_conf;		/* device configuration */

	char sc_softwake;

	struct ohci_soft_ed *sc_freeeds;
	struct ohci_soft_td *sc_freetds;
	struct ohci_soft_itd *sc_freeitds;

	struct usbd_xfer *sc_intrxfer;

	struct ohci_soft_itd *sc_sidone;
	struct ohci_soft_td *sc_sdone;

	char sc_vendor[16];
	int sc_id_vendor;

	u_int32_t sc_control;		/* Preserved during suspend/standby */
	u_int32_t sc_intre;
	u_int32_t sc_ival;

	u_int sc_overrun_cnt;
	struct timeval sc_overrun_ntc;

	struct timeout sc_tmo_rhsc;
};

struct ohci_xfer {
	struct usbd_xfer xfer;
};

usbd_status	ohci_checkrev(struct ohci_softc *);
usbd_status	ohci_handover(struct ohci_softc *);
usbd_status	ohci_init(struct ohci_softc *);
int		ohci_intr(void *);
int		ohci_detach(struct device *, int);
int		ohci_activate(struct device *, int);
