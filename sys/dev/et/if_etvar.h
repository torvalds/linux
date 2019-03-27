/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 Sepherosa Ziehau.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/dev/netif/et/if_etvar.h,v 1.4 2007/10/23 14:28:42 sephe Exp $
 * $FreeBSD$
 */

#ifndef _IF_ETVAR_H
#define _IF_ETVAR_H

#define	ET_RING_ALIGN		4096
#define	ET_STATUS_ALIGN		8
#define	ET_NSEG_MAX		32	/* XXX no limit actually */
#define	ET_NSEG_SPARE		4

#define	ET_TX_NDESC		512
#define	ET_RX_NDESC		512
#define	ET_RX_NRING		2
#define	ET_RX_NSTAT		(ET_RX_NRING * ET_RX_NDESC)

#define	ET_TX_RING_SIZE		(ET_TX_NDESC * sizeof(struct et_txdesc))
#define	ET_RX_RING_SIZE		(ET_RX_NDESC * sizeof(struct et_rxdesc))
#define	ET_RXSTAT_RING_SIZE	(ET_RX_NSTAT * sizeof(struct et_rxstat))

#define	ET_JUMBO_FRAMELEN	(ET_MEM_SIZE - ET_MEM_RXSIZE_MIN -	\
				 ET_MEM_TXSIZE_EX)
#define	ET_JUMBO_MTU		(ET_JUMBO_FRAMELEN - ETHER_HDR_LEN -	\
				 EVL_ENCAPLEN - ETHER_CRC_LEN)

#define	ET_FRAMELEN(mtu)	(ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN +	\
				 (mtu) + ETHER_CRC_LEN)

#define	ET_JSLOTS		(ET_RX_NDESC + 128)
#define	ET_JLEN			(ET_JUMBO_FRAMELEN + ETHER_ALIGN)
#define	ET_JUMBO_MEM_SIZE	(ET_JSLOTS * ET_JLEN)

#define	CSR_WRITE_4(sc, reg, val)	\
	bus_write_4((sc)->sc_mem_res, (reg), (val))
#define	CSR_READ_4(sc, reg)		\
	bus_read_4((sc)->sc_mem_res, (reg))

#define	ET_ADDR_HI(addr)	((uint64_t) (addr) >> 32)
#define	ET_ADDR_LO(addr)	((uint64_t) (addr) & 0xffffffff)

struct et_txdesc {
	uint32_t	td_addr_hi;
	uint32_t	td_addr_lo;
	uint32_t	td_ctrl1;	/* ET_TDCTRL1_ */
	uint32_t	td_ctrl2;	/* ET_TDCTRL2_ */
};

#define	ET_TDCTRL1_LEN_MASK	0x0000FFFF

#define	ET_TDCTRL2_LAST_FRAG	0x00000001
#define	ET_TDCTRL2_FIRST_FRAG	0x00000002
#define	ET_TDCTRL2_INTR		0x00000004
#define	ET_TDCTRL2_CTRL_WORD	0x00000008
#define	ET_TDCTRL2_HDX_BACKP	0x00000010
#define	ET_TDCTRL2_XMIT_PAUSE	0x00000020
#define	ET_TDCTRL2_FRAME_ERR	0x00000040
#define	ET_TDCTRL2_NO_CRC	0x00000080
#define	ET_TDCTRL2_MAC_OVRRD	0x00000100
#define	ET_TDCTRL2_PAD_PACKET	0x00000200
#define	ET_TDCTRL2_JUMBO_PACKET	0x00000400
#define	ET_TDCTRL2_INS_VLAN	0x00000800
#define	ET_TDCTRL2_CSUM_IP	0x00001000
#define	ET_TDCTRL2_CSUM_TCP	0x00002000
#define	ET_TDCTRL2_CSUM_UDP	0x00004000

struct et_rxdesc {
	uint32_t	rd_addr_lo;
	uint32_t	rd_addr_hi;
	uint32_t	rd_ctrl;	/* ET_RDCTRL_ */
};

#define	ET_RDCTRL_BUFIDX_MASK	0x000003FF

struct et_rxstat {
	uint32_t	rxst_info1;
	uint32_t	rxst_info2;	/* ET_RXST_INFO2_ */
};

#define	ET_RXST_INFO1_HASH_PASS		0x00000001
#define	ET_RXST_INFO1_IPCSUM		0x00000002
#define	ET_RXST_INFO1_IPCSUM_OK		0x00000004
#define	ET_RXST_INFO1_TCPCSUM		0x00000008
#define	ET_RXST_INFO1_TCPCSUM_OK	0x00000010
#define	ET_RXST_INFO1_WOL		0x00000020
#define	ET_RXST_INFO1_RXMAC_ERR		0x00000040
#define	ET_RXST_INFO1_DROP		0x00000080
#define	ET_RXST_INFO1_FRAME_TRUNC	0x00000100
#define	ET_RXST_INFO1_JUMBO		0x00000200
#define	ET_RXST_INFO1_VLAN		0x00000400
#define	ET_RXST_INFO1_PREV_FRMAE_DROP	0x00010000
#define	ET_RXST_INFO1_SHORT		0x00020000
#define	ET_RXST_INFO1_BAD_CARRIER	0x00040000
#define	ET_RXST_INFO1_CODE_ERR		0x00080000
#define	ET_RXST_INFO1_CRC_ERR		0x00100000
#define	ET_RXST_INFO1_LEN_MISMATCH	0x00200000
#define	ET_RXST_INFO1_TOO_LONG		0x00400000
#define	ET_RXST_INFO1_OK		0x00800000
#define	ET_RXST_INFO1_MULTICAST		0x01000000
#define	ET_RXST_INFO1_BROADCAST		0x02000000
#define	ET_RXST_INFO1_DRIBBLE		0x04000000
#define	ET_RXST_INFO1_CTL_FRAME		0x08000000
#define	ET_RXST_INFO1_PAUSE_FRAME	0x10000000
#define	ET_RXST_INFO1_UNKWN_CTL_FRAME	0x20000000
#define	ET_RXST_INFO1_VLAN_TAG		0x40000000
#define	ET_RXST_INFO1_LONG_EVENT	0x80000000

#define	ET_RXST_INFO2_LEN_MASK		0x0000FFFF
#define	ET_RXST_INFO2_LEN_SHIFT		0
#define	ET_RXST_INFO2_BUFIDX_MASK	0x03FF0000
#define	ET_RXST_INFO2_BUFIDX_SHIFT	16
#define	ET_RXST_INFO2_RINGIDX_MASK	0x0C000000
#define	ET_RXST_INFO2_RINGIDX_SHIFT	26

struct et_rxstatus {
	uint32_t	rxs_ring;
	uint32_t	rxs_stat_ring;	/* ET_RXS_STATRING_ */
};

#define	ET_RXS_STATRING_INDEX_MASK	0x0FFF0000
#define	ET_RXS_STATRING_INDEX_SHIFT	16
#define	ET_RXS_STATRING_WRAP		0x10000000

struct et_txbuf {
	struct mbuf		*tb_mbuf;
	bus_dmamap_t		tb_dmap;
};

struct et_rxbuf {
	struct mbuf		*rb_mbuf;
	bus_dmamap_t		rb_dmap;
};

struct et_txstatus_data {
	uint32_t		*txsd_status;
	bus_addr_t		txsd_paddr;
	bus_dma_tag_t		txsd_dtag;
	bus_dmamap_t		txsd_dmap;
};

struct et_rxstatus_data {
	struct et_rxstatus	*rxsd_status;
	bus_addr_t		rxsd_paddr;
	bus_dma_tag_t		rxsd_dtag;
	bus_dmamap_t		rxsd_dmap;
};

struct et_rxstat_ring {
	struct et_rxstat	*rsr_stat;
	bus_addr_t		rsr_paddr;
	bus_dma_tag_t		rsr_dtag;
	bus_dmamap_t		rsr_dmap;

	int			rsr_index;
	int			rsr_wrap;
};

struct et_txdesc_ring {
	struct et_txdesc	*tr_desc;
	bus_addr_t		tr_paddr;
	bus_dma_tag_t		tr_dtag;
	bus_dmamap_t		tr_dmap;

	int			tr_ready_index;
	int			tr_ready_wrap;
};

struct et_rxdesc_ring {
	struct et_rxdesc	*rr_desc;
	bus_addr_t		rr_paddr;
	bus_dma_tag_t		rr_dtag;
	bus_dmamap_t		rr_dmap;

	uint32_t		rr_posreg;
	int			rr_index;
	int			rr_wrap;
};

struct et_txbuf_data {
	struct et_txbuf		tbd_buf[ET_TX_NDESC];

	int			tbd_start_index;
	int			tbd_start_wrap;
	int			tbd_used;
};

struct et_softc;
struct et_rxbuf_data;

struct et_rxbuf_data {
	struct et_rxbuf		rbd_buf[ET_RX_NDESC];

	struct et_softc		*rbd_softc;
	struct et_rxdesc_ring	*rbd_ring;

	int			rbd_bufsize;
	int			(*rbd_newbuf)(struct et_rxbuf_data *, int);
	void			(*rbd_discard)(struct et_rxbuf_data *, int);
};

struct et_hw_stats {
	/* RX/TX stats. */
	uint64_t		pkts_64;
	uint64_t		pkts_65;
	uint64_t		pkts_128;
	uint64_t		pkts_256;
	uint64_t		pkts_512;
	uint64_t		pkts_1024;
	uint64_t		pkts_1519;
	/* RX stats. */
	uint64_t		rx_bytes;
	uint64_t		rx_frames;
	uint32_t		rx_crcerrs;
	uint64_t		rx_mcast;
	uint64_t		rx_bcast;
	uint32_t		rx_control;
	uint32_t		rx_pause;
	uint32_t		rx_unknown_control;
	uint32_t		rx_alignerrs;
	uint32_t		rx_lenerrs;
	uint32_t		rx_codeerrs;
	uint32_t		rx_cserrs;
	uint32_t		rx_runts;
	uint64_t		rx_oversize;
	uint32_t		rx_fragments;
	uint32_t		rx_jabbers;
	uint32_t		rx_drop;
	/* TX stats. */
	uint64_t		tx_bytes;
	uint64_t		tx_frames;
	uint64_t		tx_mcast;
	uint64_t		tx_bcast;
	uint32_t		tx_pause;
	uint32_t		tx_deferred;
	uint32_t		tx_excess_deferred;
	uint32_t		tx_single_colls;
	uint32_t		tx_multi_colls;
	uint32_t		tx_late_colls;
	uint32_t		tx_excess_colls;
	uint32_t		tx_total_colls;
	uint32_t		tx_pause_honored;
	uint32_t		tx_drop;
	uint32_t		tx_jabbers;
	uint32_t		tx_crcerrs;
	uint32_t		tx_control;
	uint64_t		tx_oversize;
	uint32_t		tx_undersize;
	uint32_t		tx_fragments;
};

struct et_softc {
	struct ifnet		*ifp;
	device_t		dev;
	struct mtx		sc_mtx;
	device_t		sc_miibus;
	void			*sc_irq_handle;
	struct resource		*sc_irq_res;
	struct resource		*sc_mem_res;

	int			sc_if_flags;
	uint32_t		sc_flags;	/* ET_FLAG_ */
	int			sc_expcap;

	int			sc_mem_rid;

	int			sc_irq_rid;

	struct callout		sc_tick;

	int			watchdog_timer;

	bus_dma_tag_t		sc_dtag;

	struct et_rxdesc_ring	sc_rx_ring[ET_RX_NRING];
	struct et_rxstat_ring	sc_rxstat_ring;
	struct et_rxstatus_data	sc_rx_status;

	struct et_txdesc_ring	sc_tx_ring;
	struct et_txstatus_data	sc_tx_status;

	bus_dma_tag_t		sc_mbuf_dtag;
	bus_dma_tag_t		sc_rx_mini_tag;
	bus_dmamap_t		sc_rx_mini_sparemap;
	bus_dma_tag_t		sc_rx_tag;
	bus_dmamap_t		sc_rx_sparemap;
	bus_dma_tag_t		sc_tx_tag;
	struct et_rxbuf_data	sc_rx_data[ET_RX_NRING];
	struct et_txbuf_data	sc_tx_data;

	struct et_hw_stats	sc_stats;
	uint32_t		sc_tx;
	uint32_t		sc_tx_intr;

	/*
	 * Sysctl variables
	 */
	int			sc_rx_intr_npkts;
	int			sc_rx_intr_delay;
	int			sc_tx_intr_nsegs;
	uint32_t		sc_timer;
};

#define	ET_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	ET_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	ET_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)

#define	ET_FLAG_PCIE		0x0001
#define	ET_FLAG_MSI		0x0002
#define	ET_FLAG_FASTETHER	0x0004
#define	ET_FLAG_TXRX_ENABLED	0x0100
#define	ET_FLAG_JUMBO		0x0200
#define	ET_FLAG_LINK		0x8000

#endif	/* !_IF_ETVAR_H */
