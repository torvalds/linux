/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2010 Nathan Whitehorn
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _POWERPC_PS3_IF_GLCREG_H
#define _POWERPC_PS3_IF_GLCREG_H

#define GLC_MAX_TX_PACKETS	128
#define GLC_MAX_RX_PACKETS	128

struct glc_dmadesc;

/*
 * software state for transmit job mbufs (may be elements of mbuf chains)
 */
 
struct glc_txsoft {
	struct mbuf *txs_mbuf;		/* head of our mbuf chain */
	bus_dmamap_t txs_dmamap;	/* our DMA map */
	int txs_firstdesc;		/* first descriptor in packet */
	int txs_lastdesc;		/* last descriptor in packet */

	int txs_ndescs;			/* number of descriptors */
	STAILQ_ENTRY(glc_txsoft) txs_q;
};

STAILQ_HEAD(glc_txsq, glc_txsoft);

/*
 * software state for receive jobs
 */
struct glc_rxsoft {
	struct mbuf *rxs_mbuf;		/* head of our mbuf chain */
	bus_dmamap_t rxs_dmamap;	/* our DMA map */

	int rxs_desc_slot;		/* DMA descriptor for this packet */
	bus_addr_t rxs_desc;

	bus_dma_segment_t segment;
};

struct glc_softc {
	struct ifnet	*sc_ifp;
	device_t	sc_self;
	struct mtx	sc_mtx;
	u_char		sc_enaddr[ETHER_ADDR_LEN];
	int		sc_tx_vlan, sc_rx_vlan;
	int		sc_ifpflags;

	uint64_t	sc_dma_base[5];
	bus_dma_tag_t	sc_dmadesc_tag;

	int		sc_irqid;
	struct resource	*sc_irq;
	void		*sc_irqctx;
	uint64_t	*sc_hwirq_status;
	volatile uint64_t sc_interrupt_status;

	struct ifmedia	sc_media;

	/* Transmission */

	bus_dma_tag_t	sc_txdma_tag;
	struct glc_txsoft sc_txsoft[GLC_MAX_TX_PACKETS];
	struct glc_dmadesc *sc_txdmadesc;
	int		next_txdma_slot, first_used_txdma_slot, bsy_txdma_slots;
	bus_dmamap_t	sc_txdmadesc_map;
	bus_addr_t	sc_txdmadesc_phys;

	struct glc_txsq	sc_txfreeq;
	struct glc_txsq	sc_txdirtyq;

	/* Reception */
	
	bus_dma_tag_t	sc_rxdma_tag;
	struct glc_rxsoft sc_rxsoft[GLC_MAX_RX_PACKETS];
	struct glc_dmadesc *sc_rxdmadesc;
	int		sc_next_rxdma_slot;
	bus_dmamap_t	sc_rxdmadesc_map;
	bus_addr_t	sc_rxdmadesc_phys;

	int		sc_bus, sc_dev;
	int		sc_wdog_timer;
	struct callout	sc_tick_ch;
};

#define GELIC_GET_MAC_ADDRESS   0x0001
#define GELIC_GET_LINK_STATUS   0x0002
#define GELIC_SET_LINK_MODE     0x0003
#define  GELIC_LINK_UP          0x0001
#define  GELIC_FULL_DUPLEX      0x0002
#define  GELIC_AUTO_NEG         0x0004
#define  GELIC_SPEED_10         0x0010
#define  GELIC_SPEED_100        0x0020
#define  GELIC_SPEED_1000       0x0040
#define GELIC_GET_VLAN_ID       0x0004
#define  GELIC_VLAN_TX_ETHERNET	0x0002
#define  GELIC_VLAN_RX_ETHERNET	0x0012
#define  GELIC_VLAN_TX_WIRELESS	0x0003
#define  GELIC_VLAN_RX_WIRELESS	0x0013

/* Command status code */
#define	GELIC_DESCR_OWNED	0xa0000000
#define	GELIC_CMDSTAT_DMA_DONE	0x00000000
#define	GELIC_CMDSTAT_CHAIN_END	0x00000002
#define GELIC_CMDSTAT_CSUM_TCP	0x00020000
#define GELIC_CMDSTAT_CSUM_UDP	0x00030000
#define GELIC_CMDSTAT_NOIPSEC	0x00080000
#define GELIC_CMDSTAT_LAST	0x00040000
#define GELIC_RXERRORS		0x7def8000

/* RX Data Status codes */
#define GELIC_RX_IPCSUM		0x20000000
#define GELIC_RX_TCPUDPCSUM	0x10000000

/* Interrupt options */
#define GELIC_INT_RXDONE	0x0000000000004000UL
#define GELIC_INT_RXFRAME	0x1000000000000000UL
#define GELIC_INT_TXDONE	0x0080000000000000UL
#define GELIC_INT_TX_CHAIN_END	0x0100000000000000UL
#define GELIC_INT_PHY		0x0000000020000000UL

/* Hardware DMA descriptor. Must be 32-byte aligned */

struct glc_dmadesc {
	uint32_t paddr;	/* Must be 128 byte aligned for receive */
	uint32_t len;
	uint32_t next;
	uint32_t cmd_stat;
	uint32_t result_size;
	uint32_t valid_size;
	uint32_t data_stat;
	uint32_t rxerror;
};

#endif /* _POWERPC_PS3_IF_GLCREG_H */

