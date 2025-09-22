/*	$OpenBSD: if_vtereg.h,v 1.2 2011/05/28 08:31:51 kevlo Exp $	*/
/*-
 * Copyright (c) 2010, Pyun YongHyeon <yongari@FreeBSD.org>
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
 * $FreeBSD: head/sys/dev/vte/if_vtereg.h 216829 2010-12-31 00:21:41Z yongari $
 */

#ifndef	_IF_VTEREG_H
#define	_IF_VTEREG_H

/* PCI low memory base and low I/O base register */
#define	VTE_PCI_LOIO			0x10
#define	VTE_PCI_LOMEM			0x14

/* MAC control register 0 */
#define	VTE_MCR0			0x00
#define	MCR0_ACCPT_ERR			0x0001
#define	MCR0_RX_ENB			0x0002
#define	MCR0_ACCPT_RUNT			0x0004
#define	MCR0_ACCPT_LONG_PKT		0x0008
#define	MCR0_ACCPT_DRIBBLE		0x0010
#define	MCR0_PROMISC			0x0020
#define	MCR0_BROADCAST_DIS		0x0040
#define	MCR0_RX_EARLY_INTR		0x0080
#define	MCR0_MULTICAST			0x0100
#define	MCR0_FC_ENB			0x0200
#define	MCR0_TX_ENB			0x1000
#define	MCR0_TX_EARLY_INTR		0x4000
#define	MCR0_FULL_DUPLEX		0x8000

/* MAC control register 1 */
#define	VTE_MCR1			0x04
#define	MCR1_MAC_RESET			0x0001
#define	MCR1_MAC_LOOPBACK		0x0002
#define	MCR1_EXCESS_COL_RETRANS_DIS	0x0004
#define	MCR1_AUTO_CHG_DUPLEX		0x0008
#define	MCR1_PKT_LENGTH_1518		0x0010
#define	MCR1_PKT_LENGTH_1522		0x0020
#define	MCR1_PKT_LENGTH_1534		0x0030
#define	MCR1_PKT_LENGTH_1537		0x0000
#define	MCR1_EARLY_INTR_THRESH_1129	0x0000
#define	MCR1_EARLY_INTR_THRESH_1257	0x0040
#define	MCR1_EARLY_INTR_THRESH_1385	0x0080
#define	MCR1_EARLY_INTR_THRESH_1513	0x00C0
#define	MCR1_EXCESS_COL_RETRY_16	0x0000
#define	MCR1_EXCESS_COL_RETRY_32	0x0100
#define	MCR1_FC_ACTIVE			0x0200
#define	MCR1_RX_DESC_HASH_IDX		0x4000
#define	MCR1_RX_UNICAST_HASH		0x8000

#define	MCR1_PKT_LENGTH_MASK		0x0030
#define	MCR1_EARLY_INTR_THRESH_MASK	0x00C0

/* MAC bus control register */
#define	VTE_MBCR			0x08
#define	MBCR_FIFO_XFER_LENGTH_4		0x0000
#define	MBCR_FIFO_XFER_LENGTH_8		0x0001
#define	MBCR_FIFO_XFER_LENGTH_16	0x0002
#define	MBCR_FIFO_XFER_LENGTH_32	0x0003
#define	MBCR_TX_FIFO_THRESH_16		0x0000
#define	MBCR_TX_FIFO_THRESH_32		0x0004
#define	MBCR_TX_FIFO_THRESH_64		0x0008
#define	MBCR_TX_FIFO_THRESH_96		0x000C
#define	MBCR_RX_FIFO_THRESH_8		0x0000
#define	MBCR_RX_FIFO_THRESH_16		0x0010
#define	MBCR_RX_FIFO_THRESH_32		0x0020
#define	MBCR_RX_FIFO_THRESH_64		0x0030
#define	MBCR_SDRAM_BUS_REQ_TIMER_MASK	0x1F00
#define	MBCR_SDRAM_BUS_REQ_TIMER_SHIFT	8
#define	MBCR_SDRAM_BUS_REQ_TIMER_DEFAULT	0x1F00

/* MAC TX interrupt control register */
#define	VTE_MTICR			0x0C
#define	MTICR_TX_TIMER_MASK		0x001F
#define	MTICR_TX_BUNDLE_MASK		0x0F00
#define	VTE_IM_TX_TIMER_DEFAULT		0x7F
#define	VTE_IM_TX_BUNDLE_DEFAULT	15

#define	VTE_IM_TIMER_MIN		0
#define	VTE_IM_TIMER_MAX		82
#define	VTE_IM_TIMER_MASK		0x001F
#define	VTE_IM_TIMER_SHIFT		0
#define	VTE_IM_BUNDLE_MIN		1
#define	VTE_IM_BUNDLE_MAX		15
#define	VTE_IM_BUNDLE_SHIFT		8

/* MAC RX interrupt control register */
#define	VTE_MRICR			0x10
#define	MRICR_RX_TIMER_MASK		0x001F
#define	MRICR_RX_BUNDLE_MASK		0x0F00
#define	VTE_IM_RX_TIMER_DEFAULT		0x7F
#define	VTE_IM_RX_BUNDLE_DEFAULT	15

/* MAC TX poll command register */
#define	VTE_TX_POLL			0x14
#define	TX_POLL_START			0x0001

/* MAC RX buffer size register */
#define	VTE_MRBSR			0x18
#define	VTE_MRBSR_SIZE_MASK		0x03FF

/* MAC RX descriptor control register */
#define	VTE_MRDCR			0x1A
#define	VTE_MRDCR_RESIDUE_MASK		0x00FF
#define	VTE_MRDCR_RX_PAUSE_THRESH_MASK	0xFF00
#define	VTE_MRDCR_RX_PAUSE_THRESH_SHIFT	8

/* MAC Last status register */
#define	VTE_MLSR			0x1C
#define	MLSR_MULTICAST			0x0001
#define	MLSR_BROADCAST			0x0002
#define	MLSR_CRC_ERR			0x0004
#define	MLSR_RUNT			0x0008
#define	MLSR_LONG_PKT			0x0010
#define	MLSR_TRUNC			0x0020
#define	MLSR_DRIBBLE			0x0040
#define	MLSR_PHY_ERR			0x0080
#define	MLSR_TX_FIFO_UNDERRUN		0x0200
#define	MLSR_RX_DESC_UNAVAIL		0x0400
#define	MLSR_TX_EXCESS_COL		0x2000
#define	MLSR_TX_LATE_COL		0x4000
#define	MLSR_RX_FIFO_OVERRUN		0x8000

/* MAC MDIO control register */
#define	VTE_MMDIO			0x20
#define	MMDIO_REG_ADDR_MASK		0x001F
#define	MMDIO_PHY_ADDR_MASK		0x1F00
#define	MMDIO_READ			0x2000
#define	MMDIO_WRITE			0x4000
#define	MMDIO_REG_ADDR_SHIFT		0
#define	MMDIO_PHY_ADDR_SHIFT		8

/* MAC MDIO read data register */
#define	VTE_MMRD			0x24
#define	MMRD_DATA_MASK			0xFFFF

/* MAC MDIO write data register */
#define	VTE_MMWD			0x28
#define	MMWD_DATA_MASK			0xFFFF

/* MAC TX descriptor start address 0 */
#define	VTE_MTDSA0			0x2C

/* MAC TX descriptor start address 1 */
#define	VTE_MTDSA1			0x30

/* MAC RX descriptor start address 0 */
#define	VTE_MRDSA0			0x34

/* MAC RX descriptor start address 1 */
#define	VTE_MRDSA1			0x38

/* MAC Interrupt status register */
#define	VTE_MISR			0x3C
#define	MISR_RX_DONE			0x0001
#define	MISR_RX_DESC_UNAVAIL		0x0002
#define	MISR_RX_FIFO_FULL		0x0004
#define	MISR_RX_EARLY_INTR		0x0008
#define	MISR_TX_DONE			0x0010
#define	MISR_TX_EARLY_INTR		0x0080
#define	MISR_EVENT_CNT_OFLOW		0x0100
#define	MISR_PHY_MEDIA_CHG		0x0200

/* MAC Interrupt enable register */
#define	VTE_MIER			0x40

#define	VTE_INTRS							\
	(MISR_RX_DONE | MISR_RX_DESC_UNAVAIL | MISR_RX_FIFO_FULL |	\
	MISR_TX_DONE | MISR_EVENT_CNT_OFLOW)

/* MAC Event counter interrupt status register */
#define	VTE_MECISR			0x44
#define	MECISR_EC_RX_DONE		0x0001
#define	MECISR_EC_MULTICAST		0x0002
#define	MECISR_EC_BROADCAST		0x0004
#define	MECISR_EC_CRC_ERR		0x0008
#define	MECISR_EC_RUNT			0x0010
#define	MESCIR_EC_LONG_PKT		0x0020
#define	MESCIR_EC_RX_DESC_UNAVAIL	0x0080
#define	MESCIR_EC_RX_FIFO_FULL		0x0100
#define	MESCIR_EC_TX_DONE		0x0200
#define	MESCIR_EC_LATE_COL		0x0400
#define	MESCIR_EC_TX_UNDERRUN		0x0800

/* MAC Event counter interrupt enable register */
#define	VTE_MECIER			0x48
#define	VTE_MECIER_INTRS						 \
	(MECISR_EC_RX_DONE | MECISR_EC_MULTICAST | MECISR_EC_BROADCAST | \
	MECISR_EC_CRC_ERR | MECISR_EC_RUNT | MESCIR_EC_LONG_PKT |	 \
	MESCIR_EC_RX_DESC_UNAVAIL | MESCIR_EC_RX_FIFO_FULL |		 \
	MESCIR_EC_TX_DONE | MESCIR_EC_LATE_COL | MESCIR_EC_TX_UNDERRUN)

#define	VTE_CNT_RX_DONE			0x50

#define	VTE_CNT_MECNT0			0x52

#define	VTE_CNT_MECNT1			0x54

#define	VTE_CNT_MECNT2			0x56

#define	VTE_CNT_MECNT3			0x58

#define	VTE_CNT_TX_DONE			0x5A

#define	VTE_CNT_MECNT4			0x5C

#define	VTE_CNT_PAUSE			0x5E

/* MAC Hash table register */
#define	VTE_MAR0			0x60
#define	VTE_MAR1			0x62
#define	VTE_MAR2			0x64
#define	VTE_MAR3			0x66

/* MAC station address and multicast address register */
#define	VTE_MID0L			0x68
#define	VTE_MID0M			0x6A
#define	VTE_MID0H			0x6C
#define	VTE_MID1L			0x70
#define	VTE_MID1M			0x72
#define	VTE_MID1H			0x74
#define	VTE_MID2L			0x78
#define	VTE_MID2M			0x7A
#define	VTE_MID2H			0x7C
#define	VTE_MID3L			0x80
#define	VTE_MID3M			0x82
#define	VTE_MID3H			0x84

#define	VTE_RXFILTER_PEEFECT_BASE	VTE_MID1L
#define	VTE_RXFILT_PERFECT_CNT		3

/* MAC PHY status change configuration register */
#define	VTE_MPSCCR			0x88
#define	MPSCCR_TIMER_DIVIDER_MASK	0x0007
#define	MPSCCR_PHY_ADDR_MASK		0x1F00
#define	MPSCCR_PHY_STS_CHG_ENB		0x8000
#define	MPSCCR_PHY_ADDR_SHIFT		8

/* MAC PHY status register2 */
#define	VTE_MPSR			0x8A
#define	MPSR_LINK_UP			0x0001
#define	MPSR_SPEED_100			0x0002
#define	MPSR_FULL_DUPLEX		0x0004

/* MAC Status machine(undocumented). */
#define	VTE_MACSM			0xAC

/* MDC Speed control register */
#define	VTE_MDCSC			0xB6
#define	MDCSC_DEFAULT			0x0030

/* MAC Identifier and revision register */
#define	VTE_MACID_REV			0xBC
#define	VTE_MACID_REV_MASK		0x00FF
#define	VTE_MACID_MASK			0xFF00
#define	VTE_MACID_REV_SHIFT		0
#define	VTE_MACID_SHIFT			8

/* MAC Identifier register */
#define	VTE_MACID			0xBE

/*
 * RX descriptor
 * - Added one more uint16_t member to align it 4 on bytes boundary.
 *   This does not affect operation of controller since it includes
 *   next pointer address.
 */
struct vte_rx_desc {
	uint16_t drst;
	uint16_t drlen;
	uint32_t drbp;
	uint32_t drnp;
	uint16_t hidx;
	uint16_t rsvd2;
	uint16_t rsvd3;
	uint16_t __pad;	/* Not actual descriptor member. */
};

#define	VTE_DRST_MID_MASK	0x0003
#define	VTE_DRST_MID_HIT	0x0004
#define	VTE_DRST_MULTICAST_HIT	0x0008
#define	VTE_DRST_MULTICAST	0x0010
#define	VTE_DRST_BROADCAST	0x0020
#define	VTE_DRST_CRC_ERR	0x0040
#define	VTE_DRST_RUNT		0x0080
#define	VTE_DRST_LONG		0x0100
#define	VTE_DRST_TRUNC		0x0200
#define	VTE_DRST_DRIBBLE	0x0400
#define	VTE_DRST_PHY_ERR	0x0800
#define	VTE_DRST_RX_OK		0x4000
#define	VTE_DRST_RX_OWN		0x8000

#define	VTE_RX_LEN(x)		((x) & 0x7FF)

#define	VTE_RX_HIDX(x)		((x) & 0x3F)

/*
 * TX descriptor
 * - Added one more uint32_t member to align it on 16 bytes boundary.
 */
struct vte_tx_desc {
	uint16_t dtst;
	uint16_t dtlen;
	uint32_t dtbp;
	uint32_t dtnp;
	uint32_t __pad;	/* Not actual descriptor member. */
};

#define	VTE_DTST_EXCESS_COL	0x0010
#define	VTE_DTST_LATE_COL	0x0020
#define	VTE_DTST_UNDERRUN	0x0040
#define	VTE_DTST_NO_CRC		0x2000
#define	VTE_DTST_TX_OK		0x4000
#define	VTE_DTST_TX_OWN		0x8000

#define	VTE_TX_LEN(x)		((x) & 0x7FF)

#define	VTE_TX_RING_CNT		64
#define	VTE_TX_RING_ALIGN	16
/*
 * The TX/RX descriptor format has no limitation for number of
 * descriptors in TX/RX ring.  However, the maximum number of
 * descriptors that could be set as RX descriptor ring residue
 * counter is 255.  This effectively limits number of RX
 * descriptors available to be less than or equal to 255.
 */
#define	VTE_RX_RING_CNT		128
#define	VTE_RX_RING_ALIGN	16
#define	VTE_RX_BUF_ALIGN	4

#define	VTE_DESC_INC(x, y)	((x) = ((x) + 1) % (y))

#define	VTE_TX_RING_SZ		\
	(sizeof(struct vte_tx_desc) * VTE_TX_RING_CNT)
#define	VTE_RX_RING_SZ		\
	(sizeof(struct vte_rx_desc) * VTE_RX_RING_CNT)

#define	VTE_RX_BUF_SIZE_MAX	(MCLBYTES - sizeof(uint32_t))

#define	VTE_MIN_FRAMELEN	(ETHER_MIN_LEN - ETHER_CRC_LEN)

struct vte_rxdesc {
	struct mbuf		*rx_m;
	bus_dmamap_t		rx_dmamap;
	struct vte_rx_desc	*rx_desc;
};

struct vte_txdesc {
	struct mbuf		*tx_m;
	bus_dmamap_t		tx_dmamap;
	struct vte_tx_desc	*tx_desc;
	int			tx_flags;
#define	VTE_TXMBUF		0x0001
};

struct vte_chain_data {
	struct vte_txdesc	vte_txdesc[VTE_TX_RING_CNT];
	struct mbuf		*vte_txmbufs[VTE_TX_RING_CNT];
	struct vte_rxdesc	vte_rxdesc[VTE_RX_RING_CNT];
	bus_dmamap_t		vte_tx_ring_map;
	bus_dma_segment_t	vte_tx_ring_seg;
	bus_dmamap_t		vte_rx_ring_map;
	bus_dma_segment_t	vte_rx_ring_seg;
	bus_dmamap_t		vte_rr_ring_map;
	bus_dma_segment_t	vte_rr_ring_seg;
	bus_dmamap_t		vte_rx_sparemap;
	bus_dmamap_t		vte_cmb_map;
	bus_dma_segment_t	vte_cmb_seg;
	bus_dmamap_t		vte_smb_map;
	bus_dma_segment_t	vte_smb_seg;
	struct vte_tx_desc	*vte_tx_ring;
	bus_addr_t		vte_tx_ring_paddr;
	struct vte_rx_desc	*vte_rx_ring;
	bus_addr_t		vte_rx_ring_paddr;

	int			vte_tx_prod;
	int			vte_tx_cons;
	int			vte_tx_cnt;
	int			vte_rx_cons;
};

struct vte_hw_stats {
	/* RX stats. */
	uint32_t rx_frames;
	uint32_t rx_bcast_frames;
	uint32_t rx_mcast_frames;
	uint32_t rx_runts;
	uint32_t rx_crcerrs;
	uint32_t rx_long_frames;
	uint32_t rx_fifo_full;
	uint32_t rx_desc_unavail;
	uint32_t rx_pause_frames;

	/* TX stats. */
	uint32_t tx_frames;
	uint32_t tx_underruns;
	uint32_t tx_late_colls;
	uint32_t tx_pause_frames;
};

/*
 * Software state per device.
 */
struct vte_softc {
	struct device		sc_dev;
	struct arpcom		sc_arpcom;
	bus_space_tag_t		sc_mem_bt;
	bus_space_handle_t	sc_mem_bh;
	bus_size_t		sc_mem_size;
	bus_dma_tag_t		sc_dmat;
	pci_chipset_tag_t	sc_pct;
	pcitag_t		sc_pcitag;
	void			*sc_irq_handle;
	struct mii_data		sc_miibus;
	uint8_t			vte_eaddr[ETHER_ADDR_LEN];
	int			vte_flags;
#define	VTE_FLAG_LINK		0x8000

	struct timeout		vte_tick_ch;
	struct vte_hw_stats	vte_stats;
	struct vte_chain_data	vte_cdata;
	int			vte_int_rx_mod;
	int			vte_int_tx_mod;
};

/* Register access macros. */
#define	CSR_WRITE_2(_sc, reg, val)	\
	bus_space_write_2((sc)->sc_mem_bt, (sc)->sc_mem_bh, (reg), (val))
#define	CSR_READ_2(_sc, reg)		\
	bus_space_read_2((sc)->sc_mem_bt, (sc)->sc_mem_bh, (reg))

#define	VTE_TX_TIMEOUT		5
#define	VTE_RESET_TIMEOUT	100
#define	VTE_TIMEOUT		1000
#define	VTE_PHY_TIMEOUT		1000

#endif	/* _IF_VTEREG_H */
