/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, Pyun YongHyeon <yongari@FreeBSD.org>
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

#ifndef	_IF_ALEVAR_H
#define	_IF_ALEVAR_H

#define	ALE_TX_RING_CNT		256	/* Should be multiple of 4. */
#define	ALE_TX_RING_CNT_MIN	32
#define	ALE_TX_RING_CNT_MAX	1020
#define	ALE_TX_RING_ALIGN	8
#define	ALE_RX_PAGE_ALIGN	32
#define	ALE_RX_PAGES		2
#define	ALE_CMB_ALIGN		32

#define	ALE_TSO_MAXSEGSIZE	4096
#define	ALE_TSO_MAXSIZE		(65535 + sizeof(struct ether_vlan_header))
#define	ALE_MAXTXSEGS		35

#define	ALE_ADDR_LO(x)		((uint64_t) (x) & 0xFFFFFFFF)
#define	ALE_ADDR_HI(x)		((uint64_t) (x) >> 32)

/* Water mark to kick reclaiming Tx buffers. */
#define	ALE_TX_DESC_HIWAT	(ALE_TX_RING_CNT - ((ALE_TX_RING_CNT * 4) / 10))

#define	ALE_MSI_MESSAGES	1
#define	ALE_MSIX_MESSAGES	1

/*
 * TODO : Should get real jumbo MTU size.
 * The hardware seems to have trouble in dealing with large
 * frame length. If you encounter unstability issue, use
 * lower MTU size.
 */
#define	ALE_JUMBO_FRAMELEN	8132
#define	ALE_JUMBO_MTU		\
	(ALE_JUMBO_FRAMELEN - sizeof(struct ether_vlan_header) - ETHER_CRC_LEN)
#define	ALE_MAX_FRAMELEN	(ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN)

#define	ALE_DESC_INC(x, y)	((x) = ((x) + 1) % (y))

struct ale_txdesc {
	struct mbuf		*tx_m;
	bus_dmamap_t		tx_dmamap;
};

struct ale_rx_page {
	bus_dma_tag_t		page_tag;
	bus_dmamap_t		page_map;
	uint8_t			*page_addr;
	bus_addr_t		page_paddr;
	bus_dma_tag_t		cmb_tag;
	bus_dmamap_t		cmb_map;
	uint32_t		*cmb_addr;
	bus_addr_t		cmb_paddr;
	uint32_t		cons;
};

struct ale_chain_data{
	bus_dma_tag_t		ale_parent_tag;
	bus_dma_tag_t		ale_buffer_tag;
	bus_dma_tag_t		ale_tx_tag;
	struct ale_txdesc	ale_txdesc[ALE_TX_RING_CNT];
	bus_dma_tag_t		ale_tx_ring_tag;
	bus_dmamap_t		ale_tx_ring_map;
	bus_dma_tag_t		ale_rx_mblock_tag[ALE_RX_PAGES];
	bus_dmamap_t		ale_rx_mblock_map[ALE_RX_PAGES];
	struct tx_desc		*ale_tx_ring;
	bus_addr_t		ale_tx_ring_paddr;
	uint32_t		*ale_tx_cmb;
	bus_addr_t		ale_tx_cmb_paddr;
	bus_dma_tag_t		ale_tx_cmb_tag;
	bus_dmamap_t		ale_tx_cmb_map;

	uint32_t		ale_tx_prod;
	uint32_t		ale_tx_cons;
	int			ale_tx_cnt;
	struct ale_rx_page	ale_rx_page[ALE_RX_PAGES];
	int			ale_rx_curp;
	uint16_t		ale_rx_seqno;
};

#define	ALE_TX_RING_SZ		\
	(sizeof(struct tx_desc) * ALE_TX_RING_CNT)
#define	ALE_RX_PAGE_SZ_MIN	(8 * 1024)
#define	ALE_RX_PAGE_SZ_MAX	(1024 * 1024)
#define	ALE_RX_FRAMES_PAGE	128
#define	ALE_RX_PAGE_SZ		\
	(roundup(ALE_MAX_FRAMELEN, ALE_RX_PAGE_ALIGN) * ALE_RX_FRAMES_PAGE)
#define	ALE_TX_CMB_SZ		(sizeof(uint32_t))
#define	ALE_RX_CMB_SZ		(sizeof(uint32_t))

#define	ALE_PROC_MIN		(ALE_RX_FRAMES_PAGE / 4)
#define	ALE_PROC_MAX		\
	((ALE_RX_PAGE_SZ * ALE_RX_PAGES) / ETHER_MAX_LEN)
#define	ALE_PROC_DEFAULT	(ALE_PROC_MAX / 4)

struct ale_hw_stats {
	/* Rx stats. */
	uint32_t rx_frames;
	uint32_t rx_bcast_frames;
	uint32_t rx_mcast_frames;
	uint32_t rx_pause_frames;
	uint32_t rx_control_frames;
	uint32_t rx_crcerrs;
	uint32_t rx_lenerrs;
	uint64_t rx_bytes;
	uint32_t rx_runts;
	uint32_t rx_fragments;
	uint32_t rx_pkts_64;
	uint32_t rx_pkts_65_127;
	uint32_t rx_pkts_128_255;
	uint32_t rx_pkts_256_511;
	uint32_t rx_pkts_512_1023;
	uint32_t rx_pkts_1024_1518;
	uint32_t rx_pkts_1519_max;
	uint32_t rx_pkts_truncated;
	uint32_t rx_fifo_oflows;
	uint32_t rx_rrs_errs;
	uint32_t rx_alignerrs;
	uint64_t rx_bcast_bytes;
	uint64_t rx_mcast_bytes;
	uint32_t rx_pkts_filtered;
	/* Tx stats. */
	uint32_t tx_frames;
	uint32_t tx_bcast_frames;
	uint32_t tx_mcast_frames;
	uint32_t tx_pause_frames;
	uint32_t tx_excess_defer;
	uint32_t tx_control_frames;
	uint32_t tx_deferred;
	uint64_t tx_bytes;
	uint32_t tx_pkts_64;
	uint32_t tx_pkts_65_127;
	uint32_t tx_pkts_128_255;
	uint32_t tx_pkts_256_511;
	uint32_t tx_pkts_512_1023;
	uint32_t tx_pkts_1024_1518;
	uint32_t tx_pkts_1519_max;
	uint32_t tx_single_colls;
	uint32_t tx_multi_colls;
	uint32_t tx_late_colls;
	uint32_t tx_excess_colls;
	uint32_t tx_abort;
	uint32_t tx_underrun;
	uint32_t tx_desc_underrun;
	uint32_t tx_lenerrs;
	uint32_t tx_pkts_truncated;
	uint64_t tx_bcast_bytes;
	uint64_t tx_mcast_bytes;
	/* Misc. */
	uint32_t reset_brk_seq;
};

/*
 * Software state per device.
 */
struct ale_softc {
	struct ifnet 		*ale_ifp;
	device_t		ale_dev;
	device_t		ale_miibus;
	struct resource		*ale_res[1];
	struct resource_spec	*ale_res_spec;
	struct resource		*ale_irq[ALE_MSI_MESSAGES];
	struct resource_spec	*ale_irq_spec;
	void			*ale_intrhand[ALE_MSI_MESSAGES];
	int			ale_rev;
	int			ale_chip_rev;
	int			ale_phyaddr;
	uint8_t			ale_eaddr[ETHER_ADDR_LEN];
	uint32_t		ale_dma_rd_burst;
	uint32_t		ale_dma_wr_burst;
	int			ale_flags;
#define	ALE_FLAG_PCIE		0x0001
#define	ALE_FLAG_PCIX		0x0002
#define	ALE_FLAG_MSI		0x0004
#define	ALE_FLAG_MSIX		0x0008
#define	ALE_FLAG_PMCAP		0x0010
#define	ALE_FLAG_FASTETHER	0x0020
#define	ALE_FLAG_JUMBO		0x0040
#define	ALE_FLAG_RXCSUM_BUG	0x0080
#define	ALE_FLAG_TXCSUM_BUG	0x0100
#define	ALE_FLAG_TXCMB_BUG	0x0200
#define	ALE_FLAG_LINK		0x8000

	struct callout		ale_tick_ch;
	struct ale_hw_stats	ale_stats;
	struct ale_chain_data	ale_cdata;
	int			ale_if_flags;
	int			ale_watchdog_timer;
	int			ale_process_limit;
	volatile int		ale_morework;
	int			ale_int_rx_mod;
	int			ale_int_tx_mod;
	int			ale_max_frame_size;
	int			ale_pagesize;

	struct task		ale_int_task;
	struct taskqueue	*ale_tq;
	struct mtx		ale_mtx;
};

/* Register access macros. */
#define	CSR_WRITE_4(_sc, reg, val)	\
	bus_write_4((_sc)->ale_res[0], (reg), (val))
#define	CSR_WRITE_2(_sc, reg, val)	\
	bus_write_2((_sc)->ale_res[0], (reg), (val))
#define	CSR_WRITE_1(_sc, reg, val)	\
	bus_write_1((_sc)->ale_res[0], (reg), (val))
#define	CSR_READ_2(_sc, reg)		\
	bus_read_2((_sc)->ale_res[0], (reg))
#define	CSR_READ_4(_sc, reg)		\
	bus_read_4((_sc)->ale_res[0], (reg))

#define	ALE_LOCK(_sc)		mtx_lock(&(_sc)->ale_mtx)
#define	ALE_UNLOCK(_sc)		mtx_unlock(&(_sc)->ale_mtx)
#define	ALE_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->ale_mtx, MA_OWNED)

#define	ALE_TX_TIMEOUT		5
#define	ALE_RESET_TIMEOUT	100
#define	ALE_TIMEOUT		1000
#define	ALE_PHY_TIMEOUT		1000

#endif	/* _IF_ATEVAR_H */
