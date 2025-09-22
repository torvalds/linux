/*	$OpenBSD: if_etreg.h,v 1.8 2025/07/14 23:49:08 jsg Exp $	*/

/*
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
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
 * $DragonFly: src/sys/dev/netif/et/if_etreg.h,v 1.1 2007/10/12 14:12:42 sephe Exp $
 */

#ifndef _IF_ETREG_H
#define _IF_ETREG_H

#define ET_INTERN_MEM_SIZE		0x400
#define ET_INTERN_MEM_END		(ET_INTERN_MEM_SIZE - 1)

/*
 * PCI registers
 *
 * ET_PCIV_ACK_LATENCY_{128,256} are from
 * PCI EXPRESS BASE SPECIFICATION, REV. 1.0a, Table 3-5
 *
 * ET_PCIV_REPLAY_TIMER_{128,256} are from
 * PCI EXPRESS BASE SPECIFICATION, REV. 1.0a, Table 3-4
 */
#define ET_PCIR_BAR			0x10

#define ET_PCIR_DEVICE_CAPS		0x4c
#define ET_PCIM_DEVICE_CAPS_MAX_PLSZ	0x7	/* Max payload size */
#define ET_PCIV_DEVICE_CAPS_PLSZ_128	0x0
#define ET_PCIV_DEVICE_CAPS_PLSZ_256	0x1

#define ET_PCIR_DEVICE_CTRL		0x50
#define ET_PCIM_DEVICE_CTRL_MAX_RRSZ	0x7000	/* Max read request size */
#define ET_PCIV_DEVICE_CTRL_RRSZ_2K	0x4000

#define ET_PCIR_MACADDR_LO		0xa4
#define ET_PCIR_MACADDR_HI		0xa8

#define ET_PCIR_EEPROM_MISC		0xb0
#define ET_PCIR_EEPROM_STATUS_MASK	0x0000ff00
#define ET_PCIM_EEPROM_STATUS_ERROR	0x00004c00

#define ET_PCIR_ACK_LATENCY		0xc0
#define ET_PCIV_ACK_LATENCY_128		237
#define ET_PCIV_ACK_LATENCY_256		416

#define ET_PCIR_REPLAY_TIMER		0xc2
#define ET_REPLAY_TIMER_RX_L0S_ADJ	250	/* XXX inferred from default */
#define ET_PCIV_REPLAY_TIMER_128	(711 + ET_REPLAY_TIMER_RX_L0S_ADJ)
#define ET_PCIV_REPLAY_TIMER_256	(1248 + ET_REPLAY_TIMER_RX_L0S_ADJ)

#define ET_PCIR_L0S_L1_LATENCY		0xcf
#define ET_PCIM_L0S_LATENCY		(7 << 0)
#define ET_PCIM_L1_LATENCY		(7 << 3)

/*
 * CSR
 */
#define ET_TXQ_START			0x0000
#define ET_TXQ_END			0x0004
#define ET_RXQ_START			0x0008
#define ET_RXQ_END			0x000c

#define ET_PM				0x0010
#define ET_PM_SYSCLK_GATE		(1 << 3)
#define ET_PM_TXCLK_GATE		(1 << 4)
#define ET_PM_RXCLK_GATE		(1 << 5)

#define ET_INTR_STATUS			0x0018
#define ET_INTR_MASK			0x001c

#define ET_SWRST			0x0028
#define ET_SWRST_TXDMA			(1U << 0)
#define ET_SWRST_RXDMA			(1U << 1)
#define ET_SWRST_TXMAC			(1U << 2)
#define ET_SWRST_RXMAC			(1U << 3)
#define ET_SWRST_MAC			(1U << 4)
#define ET_SWRST_MAC_STAT		(1U << 5)
#define ET_SWRST_MMC			(1U << 6)
#define ET_SWRST_SELFCLR_DISABLE	(1U << 31)

#define ET_MSI_CFG			0x0030

#define ET_LOOPBACK			0x0034

#define ET_TIMER			0x0038

#define ET_TXDMA_CTRL			0x1000
#define ET_TXDMA_CTRL_HALT		(1 << 0)
#define ET_TXDMA_CTRL_CACHE_THR		0xf0
#define ET_TXDMA_CTRL_SINGLE_EPKT	(1 << 8)

#define ET_TX_RING_HI			0x1004
#define ET_TX_RING_LO			0x1008
#define ET_TX_RING_CNT			0x100c

#define ET_TX_STATUS_HI			0x101c
#define ET_TX_STATUS_LO			0x1020

#define ET_TX_READY_POS			0x1024
#define ET_TX_READY_POS_INDEX		0x03ff
#define ET_TX_READY_POS_WRAP		(1 << 10)

#define ET_TX_DONE_POS			0x1060
#define ET_TX_DONE_POS_INDEX		0x03ff
#define ET_TX_DONE_POS_WRAP		(1 << 10)

#define ET_RXDMA_CTRL			0x2000
#define ET_RXDMA_CTRL_HALT		(1 << 0)
#define ET_RXDMA_CTRL_RING0_SIZE	(3 << 8)
#define ET_RXDMA_CTRL_RING0_ENABLE	(1 << 10)
#define ET_RXDMA_CTRL_RING1_SIZE	(3 << 11)
#define ET_RXDMA_CTRL_RING1_ENABLE	(1 << 13)
#define ET_RXDMA_CTRL_HALTED		(1 << 17)

#define ET_RX_STATUS_LO			0x2004
#define ET_RX_STATUS_HI			0x2008

#define ET_RX_INTR_NPKTS		0x200c
#define ET_RX_INTR_DELAY		0x2010

#define ET_RXSTAT_LO			0x2020
#define ET_RXSTAT_HI			0x2024
#define ET_RXSTAT_CNT			0x2028

#define ET_RXSTAT_POS			0x2030
#define ET_RXSTAT_POS_INDEX		0x0fff
#define ET_RXSTAT_POS_WRAP		(1 << 12)

#define ET_RXSTAT_MINCNT		0x2038

#define ET_RX_RING0_LO			0x203c
#define ET_RX_RING0_HI			0x2040
#define ET_RX_RING0_CNT			0x2044

#define ET_RX_RING0_POS			0x204c
#define ET_RX_RING0_POS_INDEX		0x03ff
#define ET_RX_RING0_POS_WRAP		(1 << 10)

#define ET_RX_RING0_MINCNT		0x2054

#define ET_RX_RING1_LO			0x2058
#define ET_RX_RING1_HI			0x205c
#define ET_RX_RING1_CNT			0x2060

#define ET_RX_RING1_POS			0x2068
#define ET_RX_RING1_POS_INDEX		0x03ff
#define ET_RX_RING1_POS_WRAP		(1 << 10)

#define ET_RX_RING1_MINCNT		0x2070

#define ET_TXMAC_CTRL			0x3000
#define ET_TXMAC_CTRL_ENABLE		(1 << 0)
#define ET_TXMAC_CTRL_FC_DISABLE	(1 << 3)

#define ET_TXMAC_FLOWCTRL		0x3010

#define ET_RXMAC_CTRL			0x4000
#define ET_RXMAC_CTRL_ENABLE		(1 << 0)
#define ET_RXMAC_CTRL_NO_PKTFILT	(1 << 2)
#define ET_RXMAC_CTRL_WOL_DISABLE	(1 << 3)

#define ET_WOL_CRC			0x4004
#define ET_WOL_SA_LO			0x4010
#define ET_WOL_SA_HI			0x4014
#define ET_WOL_MASK			0x4018

#define ET_UCAST_FILTADDR1		0x4068
#define ET_UCAST_FILTADDR2		0x406c
#define ET_UCAST_FILTADDR3		0x4070

#define ET_MULTI_HASH			0x4074

#define ET_PKTFILT			0x4084
#define ET_PKTFILT_BCAST		(1 << 0)
#define ET_PKTFILT_MCAST		(1 << 1)
#define ET_PKTFILT_UCAST		(1 << 2)
#define ET_PKTFILT_FRAG			(1 << 3)
#define ET_PKTFILT_MINLEN		0x7f0000

#define ET_RXMAC_MC_SEGSZ		0x4088
#define ET_RXMAC_MC_SEGSZ_ENABLE	(1 << 0)
#define ET_RXMAC_MC_SEGSZ_FC		(1 << 1)
#define ET_RXMAC_MC_SEGSZ_MAX		0x03fc

#define ET_RXMAC_MC_WATERMARK		0x408c
#define ET_RXMAC_SPACE_AVL		0x4094

#define ET_RXMAC_MGT			0x4098
#define ET_RXMAC_MGT_PASS_ECRC		(1 << 4)
#define ET_RXMAC_MGT_PASS_ELEN		(1 << 5)
#define ET_RXMAC_MGT_PASS_ETRUNC	(1 << 16)
#define ET_RXMAC_MGT_CHECK_PKT		(1 << 17)

#define ET_MAC_CFG1			0x5000
#define ET_MAC_CFG1_TXEN		(1U << 0)
#define ET_MAC_CFG1_SYNC_TXEN		(1U << 1)
#define ET_MAC_CFG1_RXEN		(1U << 2)
#define ET_MAC_CFG1_SYNC_RXEN		(1U << 3)
#define ET_MAC_CFG1_TXFLOW		(1U << 4)
#define ET_MAC_CFG1_RXFLOW		(1U << 5)
#define ET_MAC_CFG1_LOOPBACK		(1U << 8)
#define ET_MAC_CFG1_RST_TXFUNC		(1U << 16)
#define ET_MAC_CFG1_RST_RXFUNC		(1U << 17)
#define ET_MAC_CFG1_RST_TXMC		(1U << 18)
#define ET_MAC_CFG1_RST_RXMC		(1U << 19)
#define ET_MAC_CFG1_SIM_RST		(1U << 30)
#define ET_MAC_CFG1_SOFT_RST		(1U << 31)

#define ET_MAC_CFG2			0x5004
#define ET_MAC_CFG2_FDX			(1 << 0)
#define ET_MAC_CFG2_CRC			(1 << 1)
#define ET_MAC_CFG2_PADCRC		(1 << 2)
#define ET_MAC_CFG2_LENCHK		(1 << 4)
#define ET_MAC_CFG2_BIGFRM		(1 << 5)
#define ET_MAC_CFG2_MODE_MII		(1 << 8)
#define ET_MAC_CFG2_MODE_GMII		(1 << 9)
#define ET_MAC_CFG2_PREAMBLE_LEN	0xf000

#define ET_IPG				0x5008
#define ET_IPG_B2B			0x0000007f
#define ET_IPG_MINIFG			0x0000ff00
#define ET_IPG_NONB2B_2			0x007f0000
#define ET_IPG_NONB2B_1			0x7f000000

#define ET_MAC_HDX			0x500c
#define ET_MAC_HDX_COLLWIN		0x0003ff
#define ET_MAC_HDX_REXMIT_MAX		0x00f000
#define ET_MAC_HDX_EXC_DEFER		(1 << 16)
#define ET_MAC_HDX_NOBACKOFF		(1 << 17)
#define ET_MAC_HDX_BP_NOBACKOFF		(1 << 18)
#define ET_MAC_HDX_ALT_BEB		(1 << 19)
#define ET_MAC_HDX_ALT_BEB_TRUNC	0xf00000

#define ET_MAX_FRMLEN			0x5010

#define ET_MII_CFG			0x5020
#define ET_MII_CFG_CLKRST		(7U << 0)
#define ET_MII_CFG_PREAMBLE_SUP		(1U << 4)
#define ET_MII_CFG_SCAN_AUTOINC		(1U << 5)
#define ET_MII_CFG_RST			(1U << 31)

#define ET_MII_CMD			0x5024
#define ET_MII_CMD_READ			(1 << 0)

#define ET_MII_ADDR			0x5028
#define ET_MII_ADDR_REG			0x001f
#define ET_MII_ADDR_PHY			0x1f00
#define ET_MII_ADDR_SHIFT		8


#define ET_MII_CTRL			0x502c
#define ET_MII_CTRL_VALUE		0xffff

#define ET_MII_STAT			0x5030
#define ET_MII_STAT_VALUE		0xffff

#define ET_MII_IND			0x5034
#define ET_MII_IND_BUSY			(1 << 0)
#define ET_MII_IND_INVALID		(1 << 2)

#define ET_MAC_CTRL			0x5038
#define ET_MAC_CTRL_MODE_MII		(1 << 24)
#define ET_MAC_CTRL_LHDX		(1 << 25)
#define ET_MAC_CTRL_GHDX		(1 << 26)

#define ET_MAC_ADDR1			0x5040
#define ET_MAC_ADDR2			0x5044

#define ET_MMC_CTRL			0x7000
#define ET_MMC_CTRL_ENABLE		(1 << 0)
#define ET_MMC_CTRL_ARB_DISABLE		(1 << 1)
#define ET_MMC_CTRL_RXMAC_DISABLE	(1 << 2)
#define ET_MMC_CTRL_TXMAC_DISABLE	(1 << 3)
#define ET_MMC_CTRL_TXDMA_DISABLE	(1 << 4)
#define ET_MMC_CTRL_RXDMA_DISABLE	(1 << 5)
#define ET_MMC_CTRL_FORCE_CE		(1 << 6)

/*
 * Interrupts
 */
#define ET_INTR_TXEOF			(1 << 3)
#define ET_INTR_TXDMA_ERROR		(1 << 4)
#define ET_INTR_RXEOF			(1 << 5)
#define ET_INTR_RXRING0_LOW		(1 << 6)
#define ET_INTR_RXRING1_LOW		(1 << 7)
#define ET_INTR_RXSTAT_LOW		(1 << 8)
#define ET_INTR_RXDMA_ERROR		(1 << 9)
#define ET_INTR_TIMER			(1 << 10)
#define ET_INTR_WOL			(1 << 15)
#define ET_INTR_PHY			(1 << 16)
#define ET_INTR_TXMAC			(1 << 17)
#define ET_INTR_RXMAC			(1 << 18)
#define ET_INTR_MAC_STATS		(1 << 19)
#define ET_INTR_SLAVE_TO		(1 << 20)

#define ET_INTRS			(ET_INTR_TXEOF | \
					 ET_INTR_RXEOF | \
					 ET_INTR_TIMER)

/*
 * RX ring position uses same layout
 */
#define ET_RX_RING_POS_INDEX		(0x03ff << 0)
#define ET_RX_RING_POS_WRAP		(1 << 10)


/* $DragonFly: src/sys/dev/netif/et/if_etvar.h,v 1.1 2007/10/12 14:12:42 sephe Exp $ */

#define ET_ALIGN		0x1000
#define ET_NSEG_MAX		32	/* XXX no limit actually */
#define ET_NSEG_SPARE		5

#define ET_TX_NDESC		512
#define ET_RX_NDESC		512
#define ET_RX_NRING		2
#define ET_RX_NSTAT		(ET_RX_NRING * ET_RX_NDESC)

#define ET_TX_RING_SIZE		(ET_TX_NDESC * sizeof(struct et_txdesc))
#define ET_RX_RING_SIZE		(ET_RX_NDESC * sizeof(struct et_rxdesc))
#define ET_RXSTAT_RING_SIZE	(ET_RX_NSTAT * sizeof(struct et_rxstat))

#define CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4((sc)->sc_mem_bt, (sc)->sc_mem_bh, (reg), (val))
#define CSR_READ_4(sc, reg)		\
	bus_space_read_4((sc)->sc_mem_bt, (sc)->sc_mem_bh, (reg))

#define ET_ADDR_HI(addr)	((uint64_t) (addr) >> 32)
#define ET_ADDR_LO(addr)	((uint64_t) (addr) & 0xffffffff)

struct et_txdesc {
	uint32_t	td_addr_hi;
	uint32_t	td_addr_lo;
	uint32_t	td_ctrl1;	/* ET_TDCTRL1_ */
	uint32_t	td_ctrl2;	/* ET_TDCTRL2_ */
} __packed;

#define ET_TDCTRL1_LEN		0xffff

#define ET_TDCTRL2_LAST_FRAG	(1 << 0)
#define ET_TDCTRL2_FIRST_FRAG	(1 << 1)
#define ET_TDCTRL2_INTR		(1 << 2)

struct et_rxdesc {
	uint32_t	rd_addr_lo;
	uint32_t	rd_addr_hi;
	uint32_t	rd_ctrl;	/* ET_RDCTRL_ */
} __packed;

#define ET_RDCTRL_BUFIDX	0x03ff

struct et_rxstat {
	uint32_t	rxst_info1;
	uint32_t	rxst_info2;	/* ET_RXST_INFO2_ */
} __packed;

#define ET_RXST_INFO2_LEN	0x000ffff
#define ET_RXST_INFO2_BUFIDX	0x3ff0000
#define ET_RXST_INFO2_RINGIDX	(3 << 26)

struct et_rxstatus {
	uint32_t	rxs_ring;
	uint32_t	rxs_stat_ring;	/* ET_RXS_STATRING_ */
} __packed;

#define ET_RXS_STATRING_INDEX	0xfff0000
#define ET_RXS_STATRING_WRAP	(1 << 28)

struct et_txbuf {
	struct mbuf		*tb_mbuf;
	bus_dmamap_t		tb_dmap;
	bus_dma_segment_t	tb_seg;
};

struct et_rxbuf {
	struct mbuf		*rb_mbuf;
	bus_dmamap_t		rb_dmap;
	bus_dma_segment_t	rb_seg;
	bus_addr_t		rb_paddr;
};

struct et_txstatus_data {
	uint32_t		*txsd_status;
	bus_addr_t		txsd_paddr;
	bus_dma_tag_t		txsd_dtag;
	bus_dmamap_t		txsd_dmap;
	bus_dma_segment_t	txsd_seg;
};

struct et_rxstatus_data {
	struct et_rxstatus	*rxsd_status;
	bus_addr_t		rxsd_paddr;
	bus_dma_tag_t		rxsd_dtag;
	bus_dmamap_t		rxsd_dmap;
	bus_dma_segment_t	rxsd_seg;
};

struct et_rxstat_ring {
	struct et_rxstat	*rsr_stat;
	bus_addr_t		rsr_paddr;
	bus_dma_tag_t		rsr_dtag;
	bus_dmamap_t		rsr_dmap;
	bus_dma_segment_t	rsr_seg;

	int			rsr_index;
	int			rsr_wrap;
};

struct et_txdesc_ring {
	struct et_txdesc	*tr_desc;
	bus_addr_t		tr_paddr;
	bus_dma_tag_t		tr_dtag;
	bus_dmamap_t		tr_dmap;
	bus_dma_segment_t	tr_seg;

	int			tr_ready_index;
	int			tr_ready_wrap;
};

struct et_rxdesc_ring {
	struct et_rxdesc	*rr_desc;
	bus_addr_t		rr_paddr;
	bus_dma_tag_t		rr_dtag;
	bus_dmamap_t		rr_dmap;
	bus_dma_segment_t	rr_seg;

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
typedef int	(*et_newbuf_t)(struct et_rxbuf_data *, int, int);

struct et_rxbuf_data {
	struct et_rxbuf		rbd_buf[ET_RX_NDESC];

	struct et_softc		*rbd_softc;
	struct et_rxdesc_ring	*rbd_ring;

	int			rbd_bufsize;
	et_newbuf_t		rbd_newbuf;
};

struct et_softc {
	struct device		sc_dev;
	struct arpcom		sc_arpcom;
	int			sc_if_flags;

	bus_space_tag_t		sc_mem_bt;
	bus_space_handle_t	sc_mem_bh;
	bus_size_t		sc_mem_size;
	bus_dma_tag_t		sc_dmat;
	pci_chipset_tag_t	sc_pct;
	pcitag_t		sc_pcitag;

	void			*sc_irq_handle;

	struct mii_data		sc_miibus;
	struct timeout		sc_tick;

	struct et_rxdesc_ring	sc_rx_ring[ET_RX_NRING];
	struct et_rxstat_ring	sc_rxstat_ring;
	struct et_rxstatus_data	sc_rx_status;

	struct et_txdesc_ring	sc_tx_ring;
	struct et_txstatus_data	sc_tx_status;
	struct timeout		sc_txtick;

	bus_dmamap_t		sc_mbuf_tmp_dmap;
	struct et_rxbuf_data	sc_rx_data[ET_RX_NRING];
	struct et_txbuf_data	sc_tx_data;

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

#endif	/* !_IF_ETREG_H */
