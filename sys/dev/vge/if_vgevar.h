/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2004
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#define VGE_JUMBO_MTU	9000

#define VGE_TX_DESC_CNT		256
#define VGE_RX_DESC_CNT		252	/* Must be a multiple of 4!! */
#define VGE_TX_RING_ALIGN	64
#define VGE_RX_RING_ALIGN	64
#define VGE_MAXTXSEGS		6
#define VGE_RX_BUF_ALIGN	sizeof(uint64_t)

/*
 * VIA Velocity allows 64bit DMA addressing but high 16bits
 * of the DMA address should be the same for Tx/Rx buffers.
 * Because this condition can't be guaranteed vge(4) limit
 * DMA address space to 48bits.
 */
#if (BUS_SPACE_MAXADDR < 0xFFFFFFFFFF)
#define	VGE_BUF_DMA_MAXADDR	BUS_SPACE_MAXADDR
#else
#define	VGE_BUF_DMA_MAXADDR	0xFFFFFFFFFFFF
#endif

#define VGE_RX_LIST_SZ		(VGE_RX_DESC_CNT * sizeof(struct vge_rx_desc))
#define VGE_TX_LIST_SZ		(VGE_TX_DESC_CNT * sizeof(struct vge_tx_desc))
#define VGE_TX_DESC_INC(x)	((x) = ((x) + 1) % VGE_TX_DESC_CNT)
#define VGE_TX_DESC_DEC(x)	\
	((x) = (((x) + VGE_TX_DESC_CNT - 1) % VGE_TX_DESC_CNT))
#define VGE_RX_DESC_INC(x)	((x) = ((x) + 1) % VGE_RX_DESC_CNT)
#define VGE_ADDR_LO(y)		((uint64_t) (y) & 0xFFFFFFFF)
#define VGE_ADDR_HI(y)		((uint64_t) (y) >> 32)
#define VGE_BUFLEN(y)		((y) & 0x3FFF)
#define VGE_RXBYTES(x)		(((x) & VGE_RDSTS_BUFSIZ) >> 16)
#define VGE_MIN_FRAMELEN	60

#define	VGE_INT_HOLDOFF_TICK	20
#define	VGE_INT_HOLDOFF_USEC(x)	((x) / VGE_INT_HOLDOFF_TICK)
#define	VGE_INT_HOLDOFF_MIN	0
#define	VGE_INT_HOLDOFF_MAX	(255 * VGE_INT_HOLDOFF_TICK)
#define	VGE_INT_HOLDOFF_DEFAULT	150

#define	VGE_RX_COAL_PKT_MIN	1
#define	VGE_RX_COAL_PKT_MAX	VGE_RX_DESC_CNT
#define	VGE_RX_COAL_PKT_DEFAULT	64

#define	VGE_TX_COAL_PKT_MIN	1
#define	VGE_TX_COAL_PKT_MAX	VGE_TX_DESC_CNT
#define	VGE_TX_COAL_PKT_DEFAULT	128

struct vge_type {
	uint16_t		vge_vid;
	uint16_t		vge_did;
	char			*vge_name;
};

struct vge_txdesc {
	struct mbuf		*tx_m;
	bus_dmamap_t		tx_dmamap;
	struct vge_tx_desc	*tx_desc;
	struct vge_txdesc	*txd_prev;
};

struct vge_rxdesc {
	struct mbuf 		*rx_m;
	bus_dmamap_t		rx_dmamap;
	struct vge_rx_desc	*rx_desc;
	struct vge_rxdesc	*rxd_prev;
};

struct vge_chain_data{
	bus_dma_tag_t		vge_ring_tag;
	bus_dma_tag_t		vge_buffer_tag;
	bus_dma_tag_t		vge_tx_tag;
	struct vge_txdesc	vge_txdesc[VGE_TX_DESC_CNT];
	bus_dma_tag_t		vge_rx_tag;
	struct vge_rxdesc	vge_rxdesc[VGE_RX_DESC_CNT];
	bus_dma_tag_t		vge_tx_ring_tag;
	bus_dmamap_t		vge_tx_ring_map;
	bus_dma_tag_t		vge_rx_ring_tag;
	bus_dmamap_t		vge_rx_ring_map;
	bus_dmamap_t		vge_rx_sparemap;

	int			vge_tx_prodidx;
	int			vge_tx_considx;
	int			vge_tx_cnt;
	int			vge_rx_prodidx;
	int			vge_rx_commit;

	struct mbuf		*vge_head;
	struct mbuf		*vge_tail;
};

#define	VGE_CHAIN_RESET(_sc)						\
do {									\
	if ((_sc)->vge_cdata.vge_head != NULL) {			\
		m_freem((_sc)->vge_cdata.vge_head);			\
		(_sc)->vge_cdata.vge_head = NULL;			\
		(_sc)->vge_cdata.vge_tail = NULL;			\
	}								\
} while (0);

struct vge_ring_data {
	struct vge_tx_desc	*vge_tx_ring;
	bus_addr_t		vge_tx_ring_paddr;
	struct vge_rx_desc	*vge_rx_ring;
	bus_addr_t		vge_rx_ring_paddr;
};

struct vge_hw_stats {
	uint32_t		rx_frames;
	uint32_t		rx_good_frames;
	uint32_t		rx_fifo_oflows;
	uint32_t		rx_runts;
	uint32_t		rx_runts_errs;
	uint32_t		rx_pkts_64;
	uint32_t		rx_pkts_65_127;
	uint32_t		rx_pkts_128_255;
	uint32_t		rx_pkts_256_511;
	uint32_t		rx_pkts_512_1023;
	uint32_t		rx_pkts_1024_1518;
	uint32_t		rx_pkts_1519_max;
	uint32_t		rx_pkts_1519_max_errs;
	uint32_t		rx_jumbos;
	uint32_t		rx_crcerrs;
	uint32_t		rx_pause_frames;
	uint32_t		rx_alignerrs;
	uint32_t		rx_nobufs;
	uint32_t		rx_symerrs;
	uint32_t		rx_lenerrs;

	uint32_t		tx_good_frames;
	uint32_t		tx_pkts_64;
	uint32_t		tx_pkts_65_127;
	uint32_t		tx_pkts_128_255;
	uint32_t		tx_pkts_256_511;
	uint32_t		tx_pkts_512_1023;
	uint32_t		tx_pkts_1024_1518;
	uint32_t		tx_jumbos;
	uint32_t		tx_colls;
	uint32_t		tx_pause;
	uint32_t		tx_sqeerrs;
	uint32_t		tx_latecolls;
};

struct vge_softc {
	struct ifnet		*vge_ifp;	/* interface info */
	device_t		vge_dev;
	struct resource		*vge_res;
	struct resource		*vge_irq;
	void			*vge_intrhand;
	device_t		vge_miibus;
	int			vge_if_flags;
	int			vge_phyaddr;
	int			vge_flags;
#define	VGE_FLAG_PCIE		0x0001
#define	VGE_FLAG_MSI		0x0002
#define	VGE_FLAG_PMCAP		0x0004
#define	VGE_FLAG_JUMBO		0x0008
#define	VGE_FLAG_SUSPENDED	0x4000
#define	VGE_FLAG_LINK		0x8000
	int			vge_expcap;
	int			vge_pmcap;
	int			vge_camidx;
	int			vge_int_holdoff;
	int			vge_rx_coal_pkt;
	int			vge_tx_coal_pkt;
	struct mtx		vge_mtx;
	struct callout		vge_watchdog;
	int			vge_timer;

	struct vge_chain_data	vge_cdata;
	struct vge_ring_data	vge_rdata;
	struct vge_hw_stats	vge_stats;
};

#define	VGE_LOCK(_sc)		mtx_lock(&(_sc)->vge_mtx)
#define	VGE_UNLOCK(_sc)		mtx_unlock(&(_sc)->vge_mtx)
#define	VGE_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->vge_mtx, MA_OWNED)

/*
 * register space access macros
 */
#define CSR_WRITE_STREAM_4(sc, reg, val)	\
	bus_write_stream_4(sc->vge_res, reg, val)
#define CSR_WRITE_4(sc, reg, val)	\
	bus_write_4(sc->vge_res, reg, val)
#define CSR_WRITE_2(sc, reg, val)	\
	bus_write_2(sc->vge_res, reg, val)
#define CSR_WRITE_1(sc, reg, val)	\
	bus_write_1(sc->vge_res, reg, val)

#define CSR_READ_4(sc, reg)		\
	bus_read_4(sc->vge_res, reg)
#define CSR_READ_2(sc, reg)		\
	bus_read_2(sc->vge_res, reg)
#define CSR_READ_1(sc, reg)		\
	bus_read_1(sc->vge_res, reg)

#define CSR_SETBIT_1(sc, reg, x)	\
	CSR_WRITE_1(sc, reg, CSR_READ_1(sc, reg) | (x))
#define CSR_SETBIT_2(sc, reg, x)	\
	CSR_WRITE_2(sc, reg, CSR_READ_2(sc, reg) | (x))
#define CSR_SETBIT_4(sc, reg, x)	\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) | (x))

#define CSR_CLRBIT_1(sc, reg, x)	\
	CSR_WRITE_1(sc, reg, CSR_READ_1(sc, reg) & ~(x))
#define CSR_CLRBIT_2(sc, reg, x)	\
	CSR_WRITE_2(sc, reg, CSR_READ_2(sc, reg) & ~(x))
#define CSR_CLRBIT_4(sc, reg, x)	\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) & ~(x))

#define VGE_RXCHUNK		4
#define VGE_TIMEOUT		10000

