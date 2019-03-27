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

#ifndef	_IF_AGEVAR_H
#define	_IF_AGEVAR_H

#define	AGE_TX_RING_CNT		256
#define	AGE_RX_RING_CNT		256
#define	AGE_RR_RING_CNT		(AGE_TX_RING_CNT + AGE_RX_RING_CNT)
/* The following ring alignments are just guessing. */
#define	AGE_TX_RING_ALIGN	16
#define	AGE_RX_RING_ALIGN	16
#define	AGE_RR_RING_ALIGN	16
#define	AGE_CMB_ALIGN		16
#define	AGE_SMB_ALIGN		16

#define	AGE_TSO_MAXSEGSIZE	4096
#define	AGE_TSO_MAXSIZE		(65535 + sizeof(struct ether_vlan_header))
#define	AGE_MAXTXSEGS		35
#define	AGE_RX_BUF_ALIGN	8
#ifndef __NO_STRICT_ALIGNMENT
#define	AGE_RX_BUF_SIZE		(MCLBYTES - AGE_RX_BUF_ALIGN)	
#else
#define	AGE_RX_BUF_SIZE		(MCLBYTES)	
#endif

#define	AGE_ADDR_LO(x)		((uint64_t) (x) & 0xFFFFFFFF)
#define	AGE_ADDR_HI(x)		((uint64_t) (x) >> 32)

#define	AGE_MSI_MESSAGES	1
#define	AGE_MSIX_MESSAGES	1

/* TODO : Should get real jumbo MTU size. */
#define AGE_JUMBO_FRAMELEN	10240
#define AGE_JUMBO_MTU					\
	(AGE_JUMBO_FRAMELEN - ETHER_VLAN_ENCAP_LEN - 	\
	 ETHER_HDR_LEN - ETHER_CRC_LEN)

#define	AGE_DESC_INC(x, y)	((x) = ((x) + 1) % (y))

#define	AGE_PROC_MIN		30
#define	AGE_PROC_MAX		(AGE_RX_RING_CNT - 1)
#define	AGE_PROC_DEFAULT	(AGE_RX_RING_CNT / 2)

struct age_txdesc {
	struct mbuf		*tx_m;
	bus_dmamap_t		tx_dmamap;
	struct tx_desc		*tx_desc;
};

struct age_rxdesc {
	struct mbuf 		*rx_m;
	bus_dmamap_t		rx_dmamap;
	struct rx_desc		*rx_desc;
};

struct age_chain_data{
	bus_dma_tag_t		age_parent_tag;
	bus_dma_tag_t		age_buffer_tag;
	bus_dma_tag_t		age_tx_tag;
	struct age_txdesc	age_txdesc[AGE_TX_RING_CNT];
	bus_dma_tag_t		age_rx_tag;
	struct age_rxdesc	age_rxdesc[AGE_RX_RING_CNT];
	bus_dma_tag_t		age_tx_ring_tag;
	bus_dmamap_t		age_tx_ring_map;
	bus_dma_tag_t		age_rx_ring_tag;
	bus_dmamap_t		age_rx_ring_map;
	bus_dmamap_t		age_rx_sparemap;
	bus_dma_tag_t		age_rr_ring_tag;
	bus_dmamap_t		age_rr_ring_map;
	bus_dma_tag_t		age_cmb_block_tag;
	bus_dmamap_t		age_cmb_block_map;
	bus_dma_tag_t		age_smb_block_tag;
	bus_dmamap_t		age_smb_block_map;

	int			age_tx_prod;
	int			age_tx_cons;
	int			age_tx_cnt;
	int			age_rx_cons;
	int			age_rr_cons;
	int			age_rxlen;

	struct mbuf		*age_rxhead;
	struct mbuf		*age_rxtail;
	struct mbuf		*age_rxprev_tail;
};

struct age_ring_data {
	struct tx_desc		*age_tx_ring;
	bus_addr_t		age_tx_ring_paddr;
	struct rx_desc		*age_rx_ring;
	bus_addr_t		age_rx_ring_paddr;
	struct rx_rdesc		*age_rr_ring;
	bus_addr_t		age_rr_ring_paddr;
	struct cmb		*age_cmb_block;
	bus_addr_t		age_cmb_block_paddr;
	struct smb		*age_smb_block;
	bus_addr_t		age_smb_block_paddr;
};

#define AGE_TX_RING_SZ		\
    (sizeof(struct tx_desc) * AGE_TX_RING_CNT)
#define AGE_RX_RING_SZ		\
    (sizeof(struct rx_desc) * AGE_RX_RING_CNT)
#define	AGE_RR_RING_SZ		\
    (sizeof(struct rx_rdesc) * AGE_RR_RING_CNT)
#define	AGE_CMB_BLOCK_SZ	sizeof(struct cmb)
#define	AGE_SMB_BLOCK_SZ	sizeof(struct smb)

struct age_stats {
	/* Rx stats. */
	uint64_t rx_frames;
	uint64_t rx_bcast_frames;
	uint64_t rx_mcast_frames;
	uint32_t rx_pause_frames;
	uint32_t rx_control_frames;
	uint32_t rx_crcerrs;
	uint32_t rx_lenerrs;
	uint64_t rx_bytes;
	uint32_t rx_runts;
	uint64_t rx_fragments;
	uint64_t rx_pkts_64;
	uint64_t rx_pkts_65_127;
	uint64_t rx_pkts_128_255;
	uint64_t rx_pkts_256_511;
	uint64_t rx_pkts_512_1023;
	uint64_t rx_pkts_1024_1518;
	uint64_t rx_pkts_1519_max;
	uint64_t rx_pkts_truncated;
	uint32_t rx_fifo_oflows;
	uint32_t rx_desc_oflows;
	uint32_t rx_alignerrs;
	uint64_t rx_bcast_bytes;
	uint64_t rx_mcast_bytes;
	uint64_t rx_pkts_filtered;
	/* Tx stats. */
	uint64_t tx_frames;
	uint64_t tx_bcast_frames;
	uint64_t tx_mcast_frames;
	uint32_t tx_pause_frames;
	uint32_t tx_excess_defer;
	uint32_t tx_control_frames;
	uint32_t tx_deferred;
	uint64_t tx_bytes;
	uint64_t tx_pkts_64;
	uint64_t tx_pkts_65_127;
	uint64_t tx_pkts_128_255;
	uint64_t tx_pkts_256_511;
	uint64_t tx_pkts_512_1023;
	uint64_t tx_pkts_1024_1518;
	uint64_t tx_pkts_1519_max;
	uint32_t tx_single_colls;
	uint32_t tx_multi_colls;
	uint32_t tx_late_colls;
	uint32_t tx_excess_colls;
	uint32_t tx_underrun;
	uint32_t tx_desc_underrun;
	uint32_t tx_lenerrs;
	uint32_t tx_pkts_truncated;
	uint64_t tx_bcast_bytes;
	uint64_t tx_mcast_bytes;
};

/*
 * Software state per device.
 */
struct age_softc {
	struct ifnet 		*age_ifp;
	device_t		age_dev;
	device_t		age_miibus;
	struct resource		*age_res[1];
	struct resource_spec	*age_res_spec;
	struct resource		*age_irq[AGE_MSI_MESSAGES];
	struct resource_spec	*age_irq_spec;
	void			*age_intrhand[AGE_MSI_MESSAGES];
	int			age_rev;
	int			age_chip_rev;
	int			age_phyaddr;
	uint8_t			age_eaddr[ETHER_ADDR_LEN];
	uint32_t		age_dma_rd_burst;
	uint32_t		age_dma_wr_burst;
	int			age_flags;
#define	AGE_FLAG_PCIE		0x0001
#define	AGE_FLAG_PCIX		0x0002
#define	AGE_FLAG_MSI		0x0004
#define	AGE_FLAG_MSIX		0x0008
#define	AGE_FLAG_PMCAP		0x0010
#define	AGE_FLAG_DETACH		0x4000
#define	AGE_FLAG_LINK		0x8000

	struct callout		age_tick_ch;
	struct age_stats	age_stat;
	struct age_chain_data	age_cdata;
	struct age_ring_data	age_rdata;
	int			age_if_flags;
	int			age_watchdog_timer;
	int			age_process_limit;
	int			age_int_mod;
	int			age_max_frame_size;
	int			age_morework;
	int			age_rr_prod;
	int			age_tpd_cons;

	struct task		age_int_task;
	struct task		age_link_task;
	struct taskqueue	*age_tq;
	struct mtx		age_mtx;
};

/* Register access macros. */
#define CSR_WRITE_4(_sc, reg, val)	\
	bus_write_4((_sc)->age_res[0], (reg), (val))
#define CSR_WRITE_2(_sc, reg, val)	\
	bus_write_2((_sc)->age_res[0], (reg), (val))
#define CSR_READ_2(_sc, reg)		\
	bus_read_2((_sc)->age_res[0], (reg))
#define CSR_READ_4(_sc, reg)		\
	bus_read_4((_sc)->age_res[0], (reg))

#define AGE_LOCK(_sc)		mtx_lock(&(_sc)->age_mtx)
#define AGE_UNLOCK(_sc)		mtx_unlock(&(_sc)->age_mtx)
#define AGE_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->age_mtx, MA_OWNED)


#define	AGE_COMMIT_MBOX(_sc)						\
do {									\
	CSR_WRITE_4(_sc, AGE_MBOX,					\
	    (((_sc)->age_cdata.age_rx_cons << MBOX_RD_PROD_IDX_SHIFT) &	\
	    MBOX_RD_PROD_IDX_MASK) |					\
	    (((_sc)->age_cdata.age_rr_cons <<				\
	    MBOX_RRD_CONS_IDX_SHIFT) & MBOX_RRD_CONS_IDX_MASK) |	\
	    (((_sc)->age_cdata.age_tx_prod << MBOX_TD_PROD_IDX_SHIFT) &	\
	    MBOX_TD_PROD_IDX_MASK));					\
} while (0)

#define	AGE_RXCHAIN_RESET(_sc)						\
do {									\
	(_sc)->age_cdata.age_rxhead = NULL;				\
	(_sc)->age_cdata.age_rxtail = NULL;				\
	(_sc)->age_cdata.age_rxprev_tail = NULL;			\
	(_sc)->age_cdata.age_rxlen = 0;					\
} while (0)

#define	AGE_TX_TIMEOUT		5
#define AGE_RESET_TIMEOUT	100
#define AGE_TIMEOUT		1000
#define AGE_PHY_TIMEOUT		1000

#endif	/* _IF_AGEVAR_H */
