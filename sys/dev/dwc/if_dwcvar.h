/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 *
 * $FreeBSD$
 */

/*
 * Ethernet media access controller (EMAC)
 * Chapter 17, Altera Cyclone V Device Handbook (CV-5V2 2014.07.22)
 *
 * EMAC is an instance of the Synopsys DesignWare 3504-0
 * Universal 10/100/1000 Ethernet MAC (DWC_gmac).
 */

#ifndef	__IF_DWCVAR_H__
#define	__IF_DWCVAR_H__

/*
 * Driver data and defines.
 */
#define	RX_MAX_PACKET	0x7ff
#define	RX_DESC_COUNT	1024
#define	RX_DESC_SIZE	(sizeof(struct dwc_hwdesc) * RX_DESC_COUNT)
#define	TX_DESC_COUNT	1024
#define	TX_DESC_SIZE	(sizeof(struct dwc_hwdesc) * TX_DESC_COUNT)

struct dwc_bufmap {
	bus_dmamap_t		map;
	struct mbuf		*mbuf;
};

struct dwc_softc {
	struct resource		*res[2];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	device_t		dev;
	int			mactype;
	int			mii_clk;
	device_t		miibus;
	struct mii_data *	mii_softc;
	struct ifnet		*ifp;
	int			if_flags;
	struct mtx		mtx;
	void *			intr_cookie;
	struct callout		dwc_callout;
	boolean_t		link_is_up;
	boolean_t		is_attached;
	boolean_t		is_detaching;
	int			tx_watchdog_count;
	int			stats_harvest_count;

	/* RX */
	bus_dma_tag_t		rxdesc_tag;
	bus_dmamap_t		rxdesc_map;
	struct dwc_hwdesc	*rxdesc_ring;
	bus_addr_t		rxdesc_ring_paddr;
	bus_dma_tag_t		rxbuf_tag;
	struct dwc_bufmap	rxbuf_map[RX_DESC_COUNT];
	uint32_t		rx_idx;

	/* TX */
	bus_dma_tag_t		txdesc_tag;
	bus_dmamap_t		txdesc_map;
	struct dwc_hwdesc	*txdesc_ring;
	bus_addr_t		txdesc_ring_paddr;
	bus_dma_tag_t		txbuf_tag;
	struct dwc_bufmap	txbuf_map[TX_DESC_COUNT];
	uint32_t		tx_idx_head;
	uint32_t		tx_idx_tail;
	int			txcount;
};

#endif	/* __IF_DWCVAR_H__ */
