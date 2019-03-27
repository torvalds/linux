/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, Oleksandr Tymoshenko
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#ifndef __IF_ARGEVAR_H__
#define __IF_ARGEVAR_H__

#define	ARGE_NPHY		32
#define	ARGE_TX_RING_COUNT	128
#define	ARGE_RX_RING_COUNT	128
#define	ARGE_RX_DMA_SIZE	ARGE_RX_RING_COUNT * sizeof(struct arge_desc)
#define	ARGE_TX_DMA_SIZE	ARGE_TX_RING_COUNT * sizeof(struct arge_desc)
#define	ARGE_MAXFRAGS		8
#define ARGE_RING_ALIGN		sizeof(struct arge_desc)
#define ARGE_RX_ALIGN_4BYTE	sizeof(uint32_t)
#define ARGE_RX_ALIGN_1BYTE	sizeof(char)
#define ARGE_TX_ALIGN_4BYTE	sizeof(uint32_t)
#define ARGE_TX_ALIGN_1BYTE	sizeof(char)
#define ARGE_MAXFRAGS		8
#define	ARGE_TX_RING_ADDR(sc, i)	\
    ((sc)->arge_rdata.arge_tx_ring_paddr + sizeof(struct arge_desc) * (i))
#define	ARGE_RX_RING_ADDR(sc, i)	\
    ((sc)->arge_rdata.arge_rx_ring_paddr + sizeof(struct arge_desc) * (i))
#define	ARGE_INC(x,y)		(x) = (((x) + 1) % y)


#define	ARGE_MII_TIMEOUT	1000

#define	ARGE_LOCK(_sc)		mtx_lock(&(_sc)->arge_mtx)
#define	ARGE_UNLOCK(_sc)	mtx_unlock(&(_sc)->arge_mtx)
#define	ARGE_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->arge_mtx, MA_OWNED)

/*
 * register space access macros
 */
#define	ARGE_BARRIER_READ(sc)	bus_barrier(sc->arge_res, 0, 0, \
				    BUS_SPACE_BARRIER_READ)
#define	ARGE_BARRIER_WRITE(sc)	bus_barrier(sc->arge_res, 0, 0, \
				    BUS_SPACE_BARRIER_WRITE)
#define	ARGE_BARRIER_RW(sc)	bus_barrier(sc->arge_res, 0, 0, \
				    BUS_SPACE_BARRIER_READ | \
				    BUS_SPACE_BARRIER_WRITE)
#define ARGE_WRITE(sc, reg, val)	do {	\
		bus_write_4(sc->arge_res, (reg), (val)); \
		ARGE_BARRIER_WRITE((sc)); \
		ARGE_READ((sc), (reg)); \
	} while (0)
#define ARGE_READ(sc, reg)	 bus_read_4(sc->arge_res, (reg))

#define ARGE_SET_BITS(sc, reg, bits)	\
	ARGE_WRITE(sc, reg, ARGE_READ(sc, (reg)) | (bits))

#define ARGE_CLEAR_BITS(sc, reg, bits)	\
	ARGE_WRITE(sc, reg, ARGE_READ(sc, (reg)) & ~(bits))

/*
 * The linux driver code for the MDIO bus does a read-after-write
 * which seems to be required on MIPS74k platforms for correct
 * behaviour.
 *
 * So, ARGE_WRITE() does the write + barrier, and the following
 * ARGE_READ() seems to flush the thing all the way through the device
 * FIFO(s) before we continue issuing MDIO bus updates.
 */
#define ARGE_MDIO_WRITE(_sc, _reg, _val) \
	ARGE_WRITE((_sc), (_reg), (_val))
#define ARGE_MDIO_READ(_sc, _reg)	\
	ARGE_READ((_sc), (_reg))
#define	ARGE_MDIO_BARRIER_READ(_sc)	ARGE_BARRIER_READ(_sc)
#define	ARGE_MDIO_BARRIER_WRITE(_sc)	ARGE_BARRIER_WRITE(_sc)
#define	ARGE_MDIO_BARRIER_RW(_sc)	ARGE_BARRIER_RW(_sc)

#define ARGE_DESC_EMPTY		(1U << 31)
#define ARGE_DESC_MORE		(1 << 24)
#define ARGE_DESC_SIZE_MASK	((1 << 12) - 1)
#define	ARGE_DMASIZE(len)	((len) & ARGE_DESC_SIZE_MASK)
struct arge_desc {
	uint32_t	packet_addr;
	uint32_t	packet_ctrl;
	uint32_t	next_desc;
	uint32_t	padding;
};

struct arge_txdesc {
	struct mbuf	*tx_m;
	bus_dmamap_t	tx_dmamap;
};

struct arge_rxdesc {
	struct mbuf		*rx_m;
	bus_dmamap_t		rx_dmamap;
	struct arge_desc	*desc;
};

struct arge_chain_data {
	bus_dma_tag_t		arge_parent_tag;
	bus_dma_tag_t		arge_tx_tag;
	struct arge_txdesc	arge_txdesc[ARGE_TX_RING_COUNT];
	bus_dma_tag_t		arge_rx_tag;
	struct arge_rxdesc	arge_rxdesc[ARGE_RX_RING_COUNT];
	bus_dma_tag_t		arge_tx_ring_tag;
	bus_dma_tag_t		arge_rx_ring_tag;
	bus_dmamap_t		arge_tx_ring_map;
	bus_dmamap_t		arge_rx_ring_map;
	bus_dmamap_t		arge_rx_sparemap;
	int			arge_tx_prod;
	int			arge_tx_cons;
	int			arge_tx_cnt;
	int			arge_rx_cons;
};

struct arge_ring_data {
	struct arge_desc	*arge_rx_ring;
	struct arge_desc	*arge_tx_ring;
	bus_addr_t		arge_rx_ring_paddr;
	bus_addr_t		arge_tx_ring_paddr;
};

/*
 * Allow PLL values to be overridden.
 */
struct arge_pll_data {
	uint32_t pll_10;
	uint32_t pll_100;
	uint32_t pll_1000;
};

/*
 * Hardware specific behaviours.
 */

/*
 * Older chips support 4 byte only transmit and receive
 * addresses.
 *
 * Later chips support arbitrary TX and later later,
 * arbitrary RX addresses.
 */
#define	ARGE_HW_FLG_TX_DESC_ALIGN_4BYTE	0x00000001
#define	ARGE_HW_FLG_RX_DESC_ALIGN_4BYTE	0x00000002
#define	ARGE_HW_FLG_TX_DESC_ALIGN_1BYTE	0x00000004
#define	ARGE_HW_FLG_RX_DESC_ALIGN_1BYTE	0x00000008

struct arge_softc {
	struct ifnet		*arge_ifp;	/* interface info */
	device_t		arge_dev;
	struct ifmedia		arge_ifmedia;
	/*
	 * Media & duples settings for multiPHY MAC
	 */
	uint32_t		arge_media_type;
	uint32_t		arge_duplex_mode;
	uint32_t		arge_phymask;
	uint8_t			arge_eaddr[ETHER_ADDR_LEN];
	struct resource		*arge_res;
	int			arge_rid;
	struct resource		*arge_irq;
	void			*arge_intrhand;
	device_t		arge_miibus;
	device_t		arge_miiproxy;
	ar71xx_mii_mode		arge_miicfg;
	struct arge_pll_data	arge_pllcfg;
	bus_dma_tag_t		arge_parent_tag;
	bus_dma_tag_t		arge_tag;
	struct mtx		arge_mtx;
	struct callout		arge_stat_callout;
	struct task		arge_link_task;
	struct arge_chain_data	arge_cdata;
	struct arge_ring_data	arge_rdata;
	int			arge_link_status;
	int			arge_detach;
	uint32_t		arge_intr_status;
	int			arge_mac_unit;
	int			arge_if_flags;
	uint32_t		arge_hw_flags;
	uint32_t		arge_debug;
	uint32_t		arge_mdiofreq;
	struct {
		uint32_t	tx_pkts_unaligned;
		uint32_t	tx_pkts_unaligned_start;
		uint32_t	tx_pkts_unaligned_len;
		uint32_t	tx_pkts_nosegs;
		uint32_t	tx_pkts_aligned;
		uint32_t	rx_overflow;
		uint32_t	tx_underflow;
		uint32_t	intr_stray;
		uint32_t	intr_stray2;
		uint32_t	intr_ok;
	} stats;
	struct {
		uint32_t	count[32];
	} intr_stats;
};

#endif /* __IF_ARGEVAR_H__ */
