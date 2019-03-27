/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, Pyun YongHyeon <yongari@FreeBSD.org>
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

#ifndef	_IF_ALCVAR_H
#define	_IF_ALCVAR_H

#define	ALC_TX_RING_CNT		256
#define	ALC_TX_RING_ALIGN	sizeof(struct tx_desc)
#define	ALC_RX_RING_CNT		256
#define	ALC_RX_RING_ALIGN	sizeof(struct rx_desc)
#define	ALC_RX_BUF_ALIGN	4
#define	ALC_RR_RING_CNT		ALC_RX_RING_CNT
#define	ALC_RR_RING_ALIGN	sizeof(struct rx_rdesc)
#define	ALC_CMB_ALIGN		8
#define	ALC_SMB_ALIGN		8

#define	ALC_TSO_MAXSEGSIZE	4096
#define	ALC_TSO_MAXSIZE		(65535 + sizeof(struct ether_vlan_header))
#define	ALC_MAXTXSEGS		35

#define	ALC_ADDR_LO(x)		((uint64_t) (x) & 0xFFFFFFFF)
#define	ALC_ADDR_HI(x)		((uint64_t) (x) >> 32)

#define	ALC_DESC_INC(x, y)	((x) = ((x) + 1) % (y))

/* Water mark to kick reclaiming Tx buffers. */
#define	ALC_TX_DESC_HIWAT	((ALC_TX_RING_CNT * 6) / 10)

/*
 * AR816x controllers support up to 16 messages but this driver
 * uses single message.
 */
#define	ALC_MSI_MESSAGES	1
#define	ALC_MSIX_MESSAGES	1

#define	ALC_TX_RING_SZ		\
	(sizeof(struct tx_desc) * ALC_TX_RING_CNT)
#define	ALC_RX_RING_SZ		\
	(sizeof(struct rx_desc) * ALC_RX_RING_CNT)
#define	ALC_RR_RING_SZ		\
	(sizeof(struct rx_rdesc) * ALC_RR_RING_CNT)
#define	ALC_CMB_SZ		(sizeof(struct cmb))
#define	ALC_SMB_SZ		(sizeof(struct smb))

#define	ALC_PROC_MIN		16
#define	ALC_PROC_MAX		(ALC_RX_RING_CNT - 1)
#define	ALC_PROC_DEFAULT	(ALC_RX_RING_CNT / 4)

/*
 * The number of bits reserved for MSS in AR813x/AR815x controllers
 * are 13 bits. This limits the maximum interface MTU size in TSO
 * case(8191 + sizeof(struct ip) + sizeof(struct tcphdr)) as upper
 * stack should not generate TCP segments with MSS greater than the
 * limit. Also Atheros says that maximum MTU for TSO is 6KB.
 */
#define	ALC_TSO_MTU		(6 * 1024)

struct alc_rxdesc {
	struct mbuf		*rx_m;
	bus_dmamap_t		rx_dmamap;
	struct rx_desc		*rx_desc;
};

struct alc_txdesc {
	struct mbuf		*tx_m;
	bus_dmamap_t		tx_dmamap;
};

struct alc_ring_data {
	struct tx_desc		*alc_tx_ring;
	bus_addr_t		alc_tx_ring_paddr;
	struct rx_desc		*alc_rx_ring;
	bus_addr_t		alc_rx_ring_paddr;
	struct rx_rdesc		*alc_rr_ring;
	bus_addr_t		alc_rr_ring_paddr;
	struct cmb		*alc_cmb;
	bus_addr_t		alc_cmb_paddr;
	struct smb		*alc_smb;
	bus_addr_t		alc_smb_paddr;
};

struct alc_chain_data {
	bus_dma_tag_t		alc_parent_tag;
	bus_dma_tag_t		alc_buffer_tag;
	bus_dma_tag_t		alc_tx_tag;
	struct alc_txdesc	alc_txdesc[ALC_TX_RING_CNT];
	bus_dma_tag_t		alc_rx_tag;
	struct alc_rxdesc	alc_rxdesc[ALC_RX_RING_CNT];
	bus_dma_tag_t		alc_tx_ring_tag;
	bus_dmamap_t		alc_tx_ring_map;
	bus_dma_tag_t		alc_rx_ring_tag;
	bus_dmamap_t		alc_rx_ring_map;
	bus_dma_tag_t		alc_rr_ring_tag;
	bus_dmamap_t		alc_rr_ring_map;
	bus_dmamap_t		alc_rx_sparemap;
	bus_dma_tag_t		alc_cmb_tag;
	bus_dmamap_t		alc_cmb_map;
	bus_dma_tag_t		alc_smb_tag;
	bus_dmamap_t		alc_smb_map;

	int			alc_tx_prod;
	int			alc_tx_cons;
	int			alc_tx_cnt;
	int			alc_rx_cons;
	int			alc_rr_cons;
	int			alc_rxlen;

	struct mbuf		*alc_rxhead;
	struct mbuf		*alc_rxtail;
	struct mbuf		*alc_rxprev_tail;
};

struct alc_hw_stats {
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
};

struct alc_ident {
	uint16_t	vendorid;
	uint16_t	deviceid;
	uint32_t	max_framelen;
	const char	*name;
};

/*
 * Software state per device.
 */
struct alc_softc {
	struct ifnet 		*alc_ifp;
	device_t		alc_dev;
	device_t		alc_miibus;
	struct resource		*alc_res[1];
	struct resource_spec	*alc_res_spec;
	struct resource		*alc_irq[ALC_MSI_MESSAGES];
	struct resource_spec	*alc_irq_spec;
	void			*alc_intrhand[ALC_MSI_MESSAGES];
	struct alc_ident	*alc_ident;
	int			alc_rev;
	int			alc_chip_rev;
	int			alc_phyaddr;
	uint8_t			alc_eaddr[ETHER_ADDR_LEN];
	uint32_t		alc_dma_rd_burst;
	uint32_t		alc_dma_wr_burst;
	uint32_t		alc_rcb;
	int			alc_expcap;
	int			alc_pmcap;
	int			alc_flags;
#define	ALC_FLAG_PCIE		0x0001
#define	ALC_FLAG_PCIX		0x0002
#define	ALC_FLAG_MSI		0x0004
#define	ALC_FLAG_MSIX		0x0008
#define	ALC_FLAG_PM		0x0010
#define	ALC_FLAG_FASTETHER	0x0020
#define	ALC_FLAG_JUMBO		0x0040
#define	ALC_FLAG_CMB_BUG	0x0100
#define	ALC_FLAG_SMB_BUG	0x0200
#define	ALC_FLAG_L0S		0x0400
#define	ALC_FLAG_L1S		0x0800
#define	ALC_FLAG_APS		0x1000
#define	ALC_FLAG_AR816X_FAMILY	0x2000
#define	ALC_FLAG_LINK_WAR	0x4000
#define	ALC_FLAG_E2X00		0x8000
#define	ALC_FLAG_LINK		0x10000

	struct callout		alc_tick_ch;
	struct alc_hw_stats	alc_stats;
	struct alc_chain_data	alc_cdata;
	struct alc_ring_data	alc_rdata;
	int			alc_if_flags;
	int			alc_watchdog_timer;
	int			alc_process_limit;
	volatile int		alc_morework;
	int			alc_int_rx_mod;
	int			alc_int_tx_mod;
	int			alc_buf_size;

	struct task		alc_int_task;
	struct taskqueue	*alc_tq;
	struct mtx		alc_mtx;
};

/* Register access macros. */
#define	CSR_WRITE_4(_sc, reg, val)	\
	bus_write_4((_sc)->alc_res[0], (reg), (val))
#define	CSR_WRITE_2(_sc, reg, val)	\
	bus_write_2((_sc)->alc_res[0], (reg), (val))
#define	CSR_WRITE_1(_sc, reg, val)	\
	bus_write_1((_sc)->alc_res[0], (reg), (val))
#define	CSR_READ_2(_sc, reg)		\
	bus_read_2((_sc)->alc_res[0], (reg))
#define	CSR_READ_4(_sc, reg)		\
	bus_read_4((_sc)->alc_res[0], (reg))

#define	ALC_RXCHAIN_RESET(_sc)						\
do {									\
	(_sc)->alc_cdata.alc_rxhead = NULL;				\
	(_sc)->alc_cdata.alc_rxtail = NULL;				\
	(_sc)->alc_cdata.alc_rxprev_tail = NULL;			\
	(_sc)->alc_cdata.alc_rxlen = 0;					\
} while (0)

#define	ALC_LOCK(_sc)		mtx_lock(&(_sc)->alc_mtx)
#define	ALC_UNLOCK(_sc)		mtx_unlock(&(_sc)->alc_mtx)
#define	ALC_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->alc_mtx, MA_OWNED)

#define	ALC_TX_TIMEOUT		5
#define	ALC_RESET_TIMEOUT	100
#define	ALC_TIMEOUT		1000
#define	ALC_PHY_TIMEOUT		1000

#endif	/* _IF_ALCVAR_H */
