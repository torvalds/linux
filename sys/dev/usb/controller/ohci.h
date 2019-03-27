/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
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

#ifndef _OHCI_H_
#define	_OHCI_H_

#define	OHCI_MAX_DEVICES MIN(USB_MAX_DEVICES, 128)

#define	OHCI_NO_INTRS		32
#define	OHCI_HCCA_SIZE		256

/* Structures alignment (bytes) */
#define	OHCI_HCCA_ALIGN		256
#define	OHCI_ED_ALIGN		16
#define	OHCI_TD_ALIGN		16
#define	OHCI_ITD_ALIGN		32

#define	OHCI_PAGE_SIZE		0x1000
#define	OHCI_PAGE(x)		((x) &~ 0xfff)
#define	OHCI_PAGE_OFFSET(x)	((x) & 0xfff)
#define	OHCI_PAGE_MASK(x)	((x) & 0xfff)

#if	((USB_PAGE_SIZE < OHCI_ED_ALIGN) || (OHCI_ED_ALIGN == 0) ||	\
	(USB_PAGE_SIZE < OHCI_TD_ALIGN) || (OHCI_TD_ALIGN == 0) ||	\
	(USB_PAGE_SIZE < OHCI_ITD_ALIGN) || (OHCI_ITD_ALIGN == 0) ||	\
	(USB_PAGE_SIZE < OHCI_PAGE_SIZE) || (OHCI_PAGE_SIZE == 0))
#error	"Invalid USB page size!"
#endif

#define	OHCI_VIRTUAL_FRAMELIST_COUNT 128/* dummy */

#if (OHCI_VIRTUAL_FRAMELIST_COUNT < USB_MAX_FS_ISOC_FRAMES_PER_XFER)
#error "maximum number of full-speed isochronous frames is higher than supported!"
#endif

struct ohci_hcca {
	volatile uint32_t hcca_interrupt_table[OHCI_NO_INTRS];
	volatile uint32_t hcca_frame_number;
	volatile uint32_t hcca_done_head;
#define	OHCI_DONE_INTRS		1
} __aligned(OHCI_HCCA_ALIGN);

typedef struct ohci_hcca ohci_hcca_t;

struct ohci_ed {
	volatile uint32_t ed_flags;
#define	OHCI_ED_GET_FA(s)	((s) & 0x7f)
#define	OHCI_ED_ADDRMASK	0x0000007f
#define	OHCI_ED_SET_FA(s)	(s)
#define	OHCI_ED_GET_EN(s)	(((s) >> 7) & 0xf)
#define	OHCI_ED_SET_EN(s)	((s) << 7)
#define	OHCI_ED_DIR_MASK	0x00001800
#define	OHCI_ED_DIR_TD		0x00000000
#define	OHCI_ED_DIR_OUT		0x00000800
#define	OHCI_ED_DIR_IN		0x00001000
#define	OHCI_ED_SPEED		0x00002000
#define	OHCI_ED_SKIP		0x00004000
#define	OHCI_ED_FORMAT_GEN	0x00000000
#define	OHCI_ED_FORMAT_ISO	0x00008000
#define	OHCI_ED_GET_MAXP(s)	(((s) >> 16) & 0x07ff)
#define	OHCI_ED_SET_MAXP(s)	((s) << 16)
#define	OHCI_ED_MAXPMASK	(0x7ff << 16)
	volatile uint32_t ed_tailp;
	volatile uint32_t ed_headp;
#define	OHCI_HALTED		0x00000001
#define	OHCI_TOGGLECARRY	0x00000002
#define	OHCI_HEADMASK		0xfffffffc
	volatile uint32_t ed_next;
/*
 * Extra information needed:
 */
	struct ohci_ed *next;
	struct ohci_ed *prev;
	struct ohci_ed *obj_next;
	struct usb_page_cache *page_cache;
	uint32_t ed_self;
} __aligned(OHCI_ED_ALIGN);

typedef struct ohci_ed ohci_ed_t;

struct ohci_td {
	volatile uint32_t td_flags;
#define	OHCI_TD_R		0x00040000	/* Buffer Rounding  */
#define	OHCI_TD_DP_MASK		0x00180000	/* Direction / PID */
#define	OHCI_TD_SETUP		0x00000000
#define	OHCI_TD_OUT		0x00080000
#define	OHCI_TD_IN		0x00100000
#define	OHCI_TD_GET_DI(x)	(((x) >> 21) & 7)	/* Delay Interrupt */
#define	OHCI_TD_SET_DI(x)	((x) << 21)
#define	OHCI_TD_NOINTR		0x00e00000
#define	OHCI_TD_INTR_MASK	0x00e00000
#define	OHCI_TD_TOGGLE_CARRY	0x00000000
#define	OHCI_TD_TOGGLE_0	0x02000000
#define	OHCI_TD_TOGGLE_1	0x03000000
#define	OHCI_TD_TOGGLE_MASK	0x03000000
#define	OHCI_TD_GET_EC(x)	(((x) >> 26) & 3)	/* Error Count */
#define	OHCI_TD_GET_CC(x)	((x) >> 28)	/* Condition Code */
#define	OHCI_TD_SET_CC(x)	((x) << 28)
#define	OHCI_TD_NOCC		0xf0000000
	volatile uint32_t td_cbp;	/* Current Buffer Pointer */
	volatile uint32_t td_next;	/* Next TD */
#define	OHCI_TD_NEXT_END	0
	volatile uint32_t td_be;	/* Buffer End */
/*
 * Extra information needed:
 */
	struct ohci_td *obj_next;
	struct ohci_td *alt_next;
	struct usb_page_cache *page_cache;
	uint32_t td_self;
	uint16_t len;
} __aligned(OHCI_TD_ALIGN);

typedef struct ohci_td ohci_td_t;

struct ohci_itd {
	volatile uint32_t itd_flags;
#define	OHCI_ITD_GET_SF(x)	((x) & 0x0000ffff)
#define	OHCI_ITD_SET_SF(x)	((x) & 0xffff)
#define	OHCI_ITD_GET_DI(x)	(((x) >> 21) & 7)	/* Delay Interrupt */
#define	OHCI_ITD_SET_DI(x)	((x) << 21)
#define	OHCI_ITD_NOINTR		0x00e00000
#define	OHCI_ITD_GET_FC(x)	((((x) >> 24) & 7)+1)	/* Frame Count */
#define	OHCI_ITD_SET_FC(x)	(((x)-1) << 24)
#define	OHCI_ITD_GET_CC(x)	((x) >> 28)	/* Condition Code */
#define	OHCI_ITD_NOCC		0xf0000000
#define	OHCI_ITD_NOFFSET	8
	volatile uint32_t itd_bp0;	/* Buffer Page 0 */
	volatile uint32_t itd_next;	/* Next ITD */
	volatile uint32_t itd_be;	/* Buffer End */
	volatile uint16_t itd_offset[OHCI_ITD_NOFFSET];	/* Buffer offsets and
							 * Status */
#define	OHCI_ITD_PAGE_SELECT	0x00001000
#define	OHCI_ITD_MK_OFFS(len)	(0xe000 | ((len) & 0x1fff))
#define	OHCI_ITD_PSW_LENGTH(x)	((x) & 0xfff)	/* Transfer length */
#define	OHCI_ITD_PSW_GET_CC(x)	((x) >> 12)	/* Condition Code */
/*
 * Extra information needed:
 */
	struct ohci_itd *obj_next;
	struct usb_page_cache *page_cache;
	uint32_t itd_self;
	uint8_t	frames;
} __aligned(OHCI_ITD_ALIGN);

typedef struct ohci_itd ohci_itd_t;

#define	OHCI_CC_NO_ERROR		0
#define	OHCI_CC_CRC			1
#define	OHCI_CC_BIT_STUFFING		2
#define	OHCI_CC_DATA_TOGGLE_MISMATCH	3
#define	OHCI_CC_STALL			4
#define	OHCI_CC_DEVICE_NOT_RESPONDING	5
#define	OHCI_CC_PID_CHECK_FAILURE	6
#define	OHCI_CC_UNEXPECTED_PID		7
#define	OHCI_CC_DATA_OVERRUN		8
#define	OHCI_CC_DATA_UNDERRUN		9
#define	OHCI_CC_BUFFER_OVERRUN		12
#define	OHCI_CC_BUFFER_UNDERRUN		13
#define	OHCI_CC_NOT_ACCESSED		15

/* Some delay needed when changing certain registers. */
#define	OHCI_ENABLE_POWER_DELAY		5
#define	OHCI_READ_DESC_DELAY		5

#define	OHCI_NO_EDS			(2*OHCI_NO_INTRS)

struct ohci_hw_softc {
	struct usb_page_cache hcca_pc;
	struct usb_page_cache ctrl_start_pc;
	struct usb_page_cache bulk_start_pc;
	struct usb_page_cache isoc_start_pc;
	struct usb_page_cache intr_start_pc[OHCI_NO_EDS];

	struct usb_page hcca_pg;
	struct usb_page ctrl_start_pg;
	struct usb_page bulk_start_pg;
	struct usb_page isoc_start_pg;
	struct usb_page intr_start_pg[OHCI_NO_EDS];
};

struct ohci_config_desc {
	struct usb_config_descriptor confd;
	struct usb_interface_descriptor ifcd;
	struct usb_endpoint_descriptor endpd;
} __packed;

union ohci_hub_desc {
	struct usb_status stat;
	struct usb_port_status ps;
	struct usb_hub_descriptor hubd;
	uint8_t	temp[128];
};

typedef struct ohci_softc {
	struct ohci_hw_softc sc_hw;
	struct usb_bus sc_bus;		/* base device */
	struct usb_callout sc_tmo_rhsc;
	union ohci_hub_desc sc_hub_desc;

	struct usb_device *sc_devices[OHCI_MAX_DEVICES];
	struct resource *sc_io_res;
	struct resource *sc_irq_res;
	struct ohci_hcca *sc_hcca_p;
	struct ohci_ed *sc_ctrl_p_last;
	struct ohci_ed *sc_bulk_p_last;
	struct ohci_ed *sc_isoc_p_last;
	struct ohci_ed *sc_intr_p_last[OHCI_NO_EDS];
	void   *sc_intr_hdl;
	device_t sc_dev;
	bus_size_t sc_io_size;
	bus_space_tag_t sc_io_tag;
	bus_space_handle_t sc_io_hdl;

	uint32_t sc_eintrs;		/* enabled interrupts */

	uint16_t sc_intr_stat[OHCI_NO_EDS];
	uint16_t sc_id_vendor;

	uint8_t	sc_noport;
	uint8_t	sc_addr;		/* device address */
	uint8_t	sc_conf;		/* device configuration */
	uint8_t	sc_hub_idata[32];

	char	sc_vendor[16];

} ohci_softc_t;

usb_bus_mem_cb_t ohci_iterate_hw_softc;

usb_error_t ohci_init(ohci_softc_t *sc);
void	ohci_detach(struct ohci_softc *sc);
void	ohci_interrupt(ohci_softc_t *sc);

#endif					/* _OHCI_H_ */
