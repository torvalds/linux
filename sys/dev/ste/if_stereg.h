/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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

/*
 * Sundance PCI device/vendor ID for the
 * ST201 chip.
 */
#define ST_VENDORID		0x13F0
#define ST_DEVICEID_ST201_1	0x0200
#define ST_DEVICEID_ST201_2	0x0201

/*
 * D-Link PCI device/vendor ID for the DL10050[AB] chip
 */
#define DL_VENDORID		0x1186
#define DL_DEVICEID_DL10050	0x1002

/*
 * Register definitions for the Sundance Technologies ST201 PCI
 * fast ethernet controller. The register space is 128 bytes long and
 * can be accessed using either PCI I/O space or PCI memory mapping.
 * There are 32-bit, 16-bit and 8-bit registers.
 */

#define STE_DMACTL		0x00
#define STE_TX_DMALIST_PTR	0x04
#define STE_TX_DMABURST_THRESH	0x08
#define STE_TX_DMAURG_THRESH	0x09
#define STE_TX_DMAPOLL_PERIOD	0x0A
#define STE_RX_DMASTATUS	0x0C
#define STE_RX_DMALIST_PTR	0x10
#define STE_RX_DMABURST_THRESH	0x14
#define STE_RX_DMAURG_THRESH	0x15
#define STE_RX_DMAPOLL_PERIOD	0x16
#define	STE_COUNTDOWN		0x18
#define STE_DEBUGCTL		0x1A
#define STE_ASICCTL		0x30
#define STE_EEPROM_DATA		0x34
#define STE_EEPROM_CTL		0x36
#define STE_FIFOCTL		0x3A
#define STE_TX_STARTTHRESH	0x3C
#define STE_RX_EARLYTHRESH	0x3E
#define STE_EXT_ROMADDR		0x40
#define STE_EXT_ROMDATA		0x44
#define STE_WAKE_EVENT		0x45
#define STE_TX_STATUS		0x46
#define STE_TX_FRAMEID		0x47
#define STE_ISR_ACK		0x4A
#define STE_IMR			0x4C
#define STE_ISR			0x4E
#define STE_MACCTL0		0x50
#define STE_MACCTL1		0x52
#define STE_PAR0		0x54
#define STE_PAR1		0x56
#define STE_PAR2		0x58
#define STE_MAX_FRAMELEN	0x5A
#define STE_RX_MODE		0x5C
#define STE_TX_RECLAIM_THRESH	0x5D
#define STE_PHYCTL		0x5E
#define STE_MAR0		0x60
#define STE_MAR1		0x62
#define STE_MAR2		0x64
#define STE_MAR3		0x66

#define	STE_STAT_RX_OCTETS_LO	0x68
#define	STE_STAT_RX_OCTETS_HI	0x6A
#define	STE_STAT_TX_OCTETS_LO	0x6C
#define	STE_STAT_TX_OCTETS_HI	0x6E
#define	STE_STAT_TX_FRAMES	0x70
#define	STE_STAT_RX_FRAMES	0x72
#define	STE_STAT_CARRIER_ERR	0x74
#define	STE_STAT_LATE_COLLS	0x75
#define	STE_STAT_MULTI_COLLS	0x76
#define	STE_STAT_SINGLE_COLLS	0x77
#define	STE_STAT_TX_DEFER	0x78
#define	STE_STAT_RX_LOST	0x79
#define	STE_STAT_TX_EXDEFER	0x7A
#define	STE_STAT_TX_ABORT	0x7B
#define	STE_STAT_TX_BCAST	0x7C
#define	STE_STAT_RX_BCAST	0x7D
#define	STE_STAT_TX_MCAST	0x7E
#define	STE_STAT_RX_MCAST	0x7F

#define STE_DMACTL_RXDMA_STOPPED	0x00000001
#define STE_DMACTL_TXDMA_CMPREQ		0x00000002
#define STE_DMACTL_TXDMA_STOPPED	0x00000004
#define STE_DMACTL_RXDMA_COMPLETE	0x00000008
#define STE_DMACTL_TXDMA_COMPLETE	0x00000010
#define STE_DMACTL_RXDMA_STALL		0x00000100
#define STE_DMACTL_RXDMA_UNSTALL	0x00000200
#define STE_DMACTL_TXDMA_STALL		0x00000400
#define STE_DMACTL_TXDMA_UNSTALL	0x00000800
#define STE_DMACTL_TXDMA_INPROG		0x00004000
#define STE_DMACTL_DMA_HALTINPROG	0x00008000
#define STE_DMACTL_RXEARLY_ENABLE	0x00020000
#define STE_DMACTL_COUNTDOWN_SPEED	0x00040000
#define STE_DMACTL_COUNTDOWN_MODE	0x00080000
#define STE_DMACTL_MWI_DISABLE		0x00100000
#define STE_DMACTL_RX_DISCARD_OFLOWS	0x00400000
#define STE_DMACTL_COUNTDOWN_ENABLE	0x00800000
#define STE_DMACTL_TARGET_ABORT		0x40000000
#define STE_DMACTL_MASTER_ABORT		0x80000000

/*
 * TX DMA burst thresh is the number of 32-byte blocks that
 * must be loaded into the TX Fifo before a TXDMA burst request
 * will be issued.
 */
#define STE_TXDMABURST_THRESH		0x1F

/*
 * The number of 32-byte blocks in the TX FIFO falls below the
 * TX DMA urgent threshold, a TX DMA urgent request will be
 * generated.
 */
#define STE_TXDMAURG_THRESH		0x3F

/*
 * Number of 320ns intervals between polls of the TXDMA next
 * descriptor pointer (if we're using polling mode).
 */
#define STE_TXDMA_POLL_PERIOD		0x7F

#define STE_RX_DMASTATUS_FRAMELEN	0x00001FFF
#define STE_RX_DMASTATUS_RXERR		0x00004000
#define STE_RX_DMASTATUS_DMADONE	0x00008000
#define STE_RX_DMASTATUS_FIFO_OFLOW	0x00010000
#define STE_RX_DMASTATUS_RUNT		0x00020000
#define STE_RX_DMASTATUS_ALIGNERR	0x00040000
#define STE_RX_DMASTATUS_CRCERR		0x00080000
#define STE_RX_DMASTATUS_GIANT		0x00100000
#define STE_RX_DMASTATUS_DRIBBLE	0x00800000
#define STE_RX_DMASTATUS_DMA_OFLOW	0x01000000

/*
 * RX DMA burst thresh is the number of 32-byte blocks that
 * must be present in the RX FIFO before a RXDMA bus master
 * request will be issued.
 */
#define STE_RXDMABURST_THRESH		0xFF

/*
 * The number of 32-byte blocks in the RX FIFO falls below the
 * RX DMA urgent threshold, a RX DMA urgent request will be
 * generated.
 */
#define STE_RXDMAURG_THRESH		0x1F

/*
 * Number of 320ns intervals between polls of the RXDMA complete
 * bit in the status field on the current RX descriptor (if we're
 * using polling mode).
 */
#define STE_RXDMA_POLL_PERIOD		0x7F

#define STE_DEBUGCTL_GPIO0_CTL		0x0001
#define STE_DEBUGCTL_GPIO1_CTL		0x0002
#define STE_DEBUGCTL_GPIO0_DATA		0x0004
#define STE_DEBUGCTL_GPIO1_DATA		0x0008

#define STE_ASICCTL_ROMSIZE		0x00000002
#define STE_ASICCTL_TX_LARGEPKTS	0x00000004
#define STE_ASICCTL_RX_LARGEPKTS	0x00000008
#define STE_ASICCTL_EXTROM_DISABLE	0x00000010
#define STE_ASICCTL_PHYSPEED_10		0x00000020
#define STE_ASICCTL_PHYSPEED_100	0x00000040
#define STE_ASICCTL_PHYMEDIA		0x00000080
#define STE_ASICCTL_FORCEDCONFIG	0x00000700
#define STE_ASICCTL_D3RESET_DISABLE	0x00000800
#define STE_ASICCTL_SPEEDUPMODE		0x00002000
#define STE_ASICCTL_LEDMODE		0x00004000
#define STE_ASICCTL_RSTOUT_POLARITY	0x00008000
#define STE_ASICCTL_GLOBAL_RESET	0x00010000
#define STE_ASICCTL_RX_RESET		0x00020000
#define STE_ASICCTL_TX_RESET		0x00040000
#define STE_ASICCTL_DMA_RESET		0x00080000
#define STE_ASICCTL_FIFO_RESET		0x00100000
#define STE_ASICCTL_NETWORK_RESET	0x00200000
#define STE_ASICCTL_HOST_RESET		0x00400000
#define STE_ASICCTL_AUTOINIT_RESET	0x00800000
#define STE_ASICCTL_EXTRESET_RESET	0x01000000
#define STE_ASICCTL_SOFTINTR		0x02000000
#define STE_ASICCTL_RESET_BUSY		0x04000000

#define STE_EECTL_ADDR			0x00FF
#define STE_EECTL_OPCODE		0x0300
#define STE_EECTL_BUSY			0x1000

#define STE_EEOPCODE_WRITE		0x0100
#define STE_EEOPCODE_READ		0x0200
#define STE_EEOPCODE_ERASE		0x0300

#define STE_FIFOCTL_RAMTESTMODE		0x0001
#define STE_FIFOCTL_OVERRUNMODE		0x0200
#define STE_FIFOCTL_RXFIFOFULL		0x0800
#define STE_FIFOCTL_TX_BUSY		0x4000
#define STE_FIFOCTL_RX_BUSY		0x8000

/*
 * The number of bytes that must in present in the TX FIFO before
 * transmission begins. Value should be in increments of 4 bytes.
 */
#define STE_TXSTART_THRESH		0x1FFC

/*
 * Number of bytes that must be present in the RX FIFO before
 * an RX EARLY interrupt is generated.
 */
#define STE_RXEARLY_THRESH		0x1FFC

#define STE_WAKEEVENT_WAKEPKT_ENB	0x01
#define STE_WAKEEVENT_MAGICPKT_ENB	0x02
#define STE_WAKEEVENT_LINKEVT_ENB	0x04
#define STE_WAKEEVENT_WAKEPOLARITY	0x08
#define STE_WAKEEVENT_WAKEPKTEVENT	0x10
#define STE_WAKEEVENT_MAGICPKTEVENT	0x20
#define STE_WAKEEVENT_LINKEVENT		0x40
#define STE_WAKEEVENT_WAKEONLAN_ENB	0x80

#define STE_TXSTATUS_RECLAIMERR		0x02
#define STE_TXSTATUS_STATSOFLOW		0x04
#define STE_TXSTATUS_EXCESSCOLLS	0x08
#define STE_TXSTATUS_UNDERRUN		0x10
#define STE_TXSTATUS_TXINTR_REQ		0x40
#define STE_TXSTATUS_TXDONE		0x80

#define	STE_ERR_BITS			"\20"				\
					"\2RECLAIM\3STSOFLOW"		\
					"\4EXCESSCOLLS\5UNDERRUN"	\
					"\6INTREQ\7DONE"

#define STE_ISRACK_INTLATCH		0x0001
#define STE_ISRACK_HOSTERR		0x0002
#define STE_ISRACK_TX_DONE		0x0004
#define STE_ISRACK_MACCTL_FRAME		0x0008
#define STE_ISRACK_RX_DONE		0x0010
#define STE_ISRACK_RX_EARLY		0x0020
#define STE_ISRACK_SOFTINTR		0x0040
#define STE_ISRACK_STATS_OFLOW		0x0080
#define STE_ISRACK_LINKEVENT		0x0100
#define STE_ISRACK_TX_DMADONE		0x0200
#define STE_ISRACK_RX_DMADONE		0x0400

#define STE_IMR_HOSTERR			0x0002
#define STE_IMR_TX_DONE			0x0004
#define STE_IMR_MACCTL_FRAME		0x0008
#define STE_IMR_RX_DONE			0x0010
#define STE_IMR_RX_EARLY		0x0020
#define STE_IMR_SOFTINTR		0x0040
#define STE_IMR_STATS_OFLOW		0x0080
#define STE_IMR_LINKEVENT		0x0100
#define STE_IMR_TX_DMADONE		0x0200
#define STE_IMR_RX_DMADONE		0x0400

#define STE_INTRS				\
	(STE_IMR_RX_DMADONE|STE_IMR_TX_DMADONE|	\
	STE_IMR_TX_DONE|STE_IMR_SOFTINTR|	\
	STE_IMR_HOSTERR)

#define STE_ISR_INTLATCH		0x0001
#define STE_ISR_HOSTERR			0x0002
#define STE_ISR_TX_DONE			0x0004
#define STE_ISR_MACCTL_FRAME		0x0008
#define STE_ISR_RX_DONE			0x0010
#define STE_ISR_RX_EARLY		0x0020
#define STE_ISR_SOFTINTR		0x0040
#define STE_ISR_STATS_OFLOW		0x0080
#define STE_ISR_LINKEVENT		0x0100
#define STE_ISR_TX_DMADONE		0x0200
#define STE_ISR_RX_DMADONE		0x0400

/*
 * Note: the Sundance manual gives the impression that the's
 * only one 32-bit MACCTL register. In fact, there are two
 * 16-bit registers side by side, and you have to access them
 * separately.
 */
#define STE_MACCTL0_IPG			0x0003
#define STE_MACCTL0_FULLDUPLEX		0x0020
#define STE_MACCTL0_RX_GIANTS		0x0040
#define STE_MACCTL0_FLOWCTL_ENABLE	0x0100
#define STE_MACCTL0_RX_FCS		0x0200
#define STE_MACCTL0_FIFOLOOPBK		0x0400
#define STE_MACCTL0_MACLOOPBK		0x0800

#define STE_MACCTL1_COLLDETECT		0x0001
#define STE_MACCTL1_CARRSENSE		0x0002
#define STE_MACCTL1_TX_BUSY		0x0004
#define STE_MACCTL1_TX_ERROR		0x0008
#define STE_MACCTL1_STATS_ENABLE	0x0020
#define STE_MACCTL1_STATS_DISABLE	0x0040
#define STE_MACCTL1_STATS_ENABLED	0x0080
#define STE_MACCTL1_TX_ENABLE		0x0100
#define STE_MACCTL1_TX_DISABLE		0x0200
#define STE_MACCTL1_TX_ENABLED		0x0400
#define STE_MACCTL1_RX_ENABLE		0x0800
#define STE_MACCTL1_RX_DISABLE		0x1000
#define STE_MACCTL1_RX_ENABLED		0x2000
#define STE_MACCTL1_PAUSED		0x4000

#define STE_IPG_96BT			0x00000000
#define STE_IPG_128BT			0x00000001
#define STE_IPG_224BT			0x00000002
#define STE_IPG_544BT			0x00000003

#define STE_RXMODE_UNICAST		0x01
#define STE_RXMODE_ALLMULTI		0x02
#define STE_RXMODE_BROADCAST		0x04
#define STE_RXMODE_PROMISC		0x08
#define STE_RXMODE_MULTIHASH		0x10
#define STE_RXMODE_ALLIPMULTI		0x20

#define STE_PHYCTL_MCLK			0x01
#define STE_PHYCTL_MDATA		0x02
#define STE_PHYCTL_MDIR			0x04
#define STE_PHYCTL_CLK25_DISABLE	0x08
#define STE_PHYCTL_DUPLEXPOLARITY	0x10
#define STE_PHYCTL_DUPLEXSTAT		0x20
#define STE_PHYCTL_SPEEDSTAT		0x40
#define STE_PHYCTL_LINKSTAT		0x80

#define	STE_TIMER_TICKS			32
#define	STE_TIMER_USECS(x)		((x * 10) / STE_TIMER_TICKS)

#define	STE_IM_RX_TIMER_MIN		0
#define	STE_IM_RX_TIMER_MAX		209712
#define	STE_IM_RX_TIMER_DEFAULT		150

/*
 * EEPROM offsets.
 */
#define STE_EEADDR_CONFIGPARM		0x00
#define STE_EEADDR_ASICCTL		0x02
#define STE_EEADDR_SUBSYS_ID		0x04
#define STE_EEADDR_SUBVEN_ID		0x08

#define STE_EEADDR_NODE0		0x10
#define STE_EEADDR_NODE1		0x12
#define STE_EEADDR_NODE2		0x14

/* PCI registers */
#define STE_PCI_VENDOR_ID		0x00
#define STE_PCI_DEVICE_ID		0x02
#define STE_PCI_COMMAND			0x04
#define STE_PCI_STATUS			0x06
#define STE_PCI_CLASSCODE		0x09
#define STE_PCI_LATENCY_TIMER		0x0D
#define STE_PCI_HEADER_TYPE		0x0E
#define STE_PCI_LOIO			0x10
#define STE_PCI_LOMEM			0x14
#define STE_PCI_BIOSROM			0x30
#define STE_PCI_INTLINE			0x3C
#define STE_PCI_INTPIN			0x3D
#define STE_PCI_MINGNT			0x3E
#define STE_PCI_MINLAT			0x0F

#define STE_PCI_CAPID			0x50 /* 8 bits */
#define STE_PCI_NEXTPTR			0x51 /* 8 bits */
#define STE_PCI_PWRMGMTCAP		0x52 /* 16 bits */
#define STE_PCI_PWRMGMTCTRL		0x54 /* 16 bits */

#define STE_PSTATE_MASK			0x0003
#define STE_PSTATE_D0			0x0000
#define STE_PSTATE_D1			0x0002
#define STE_PSTATE_D2			0x0002
#define STE_PSTATE_D3			0x0003
#define STE_PME_EN			0x0010
#define STE_PME_STATUS			0x8000

struct ste_hw_stats {
	uint64_t		rx_bytes;
	uint32_t		rx_frames;
	uint32_t		rx_bcast_frames;
	uint32_t		rx_mcast_frames;
	uint32_t		rx_lost_frames;
	uint64_t		tx_bytes;
	uint32_t		tx_frames;
	uint32_t		tx_bcast_frames;
	uint32_t		tx_mcast_frames;
	uint32_t		tx_carrsense_errs;
	uint32_t		tx_single_colls;
	uint32_t		tx_multi_colls;
	uint32_t		tx_late_colls;
	uint32_t		tx_frames_defered;
	uint32_t		tx_excess_defers;
	uint32_t		tx_abort;
};

struct ste_frag {
	uint32_t		ste_addr;
	uint32_t		ste_len;
};

#define STE_FRAG_LAST		0x80000000
#define STE_FRAG_LEN		0x00001FFF

/*
 * A TFD is 16 to 512 bytes in length which means it can have up to 126
 * fragments for a single Tx frame. Since most frames used in stack have
 * 3-4 fragments supporting 8 fragments would be enough for normal
 * operation. If we encounter more than 8 fragments we'll collapse them
 * into a frame that has less than or equal to 8 fragments. Each buffer
 * address of a fragment has no alignment limitation.
 */
#define STE_MAXFRAGS	8

struct ste_desc {
	uint32_t		ste_next;
	uint32_t		ste_ctl;
	struct ste_frag		ste_frags[STE_MAXFRAGS];
};

/*
 * A RFD has the same structure of TFD which in turn means hardware
 * supports scatter operation in Rx buffer. Since we just allocate Rx
 * buffer with m_getcl(9) there is no fragmentation at all so use
 * single fragment for RFD.
 */
struct ste_desc_onefrag {
	uint32_t		ste_next;
	uint32_t		ste_status;
	struct ste_frag		ste_frag;
};

#define STE_TXCTL_WORDALIGN	0x00000003
#define STE_TXCTL_ALIGN_DIS	0x00000001
#define STE_TXCTL_FRAMEID	0x000003FC
#define STE_TXCTL_NOCRC		0x00002000
#define STE_TXCTL_TXINTR	0x00008000
#define STE_TXCTL_DMADONE	0x00010000
#define STE_TXCTL_DMAINTR	0x80000000

#define STE_RXSTAT_FRAMELEN	0x00001FFF
#define STE_RXSTAT_FRAME_ERR	0x00004000
#define STE_RXSTAT_DMADONE	0x00008000
#define STE_RXSTAT_FIFO_OFLOW	0x00010000
#define STE_RXSTAT_RUNT		0x00020000
#define STE_RXSTAT_ALIGNERR	0x00040000
#define STE_RXSTAT_CRCERR	0x00080000
#define STE_RXSTAT_GIANT	0x00100000
#define STE_RXSTAT_DRIBBLEBITS	0x00800000
#define STE_RXSTAT_DMA_OFLOW	0x01000000
#define STE_RXATAT_ONEBUF	0x10000000

#define STE_RX_BYTES(x)		((x) & STE_RXSTAT_FRAMELEN)

/*
 * register space access macros
 */
#define CSR_WRITE_4(sc, reg, val)	\
	bus_write_4((sc)->ste_res, reg, val)
#define CSR_WRITE_2(sc, reg, val)	\
	bus_write_2((sc)->ste_res, reg, val)
#define CSR_WRITE_1(sc, reg, val)	\
	bus_write_1((sc)->ste_res, reg, val)

#define CSR_READ_4(sc, reg)		\
	bus_read_4((sc)->ste_res, reg)
#define CSR_READ_2(sc, reg)		\
	bus_read_2((sc)->ste_res, reg)
#define CSR_READ_1(sc, reg)		\
	bus_read_1((sc)->ste_res, reg)

#define	CSR_BARRIER(sc, reg, length, flags)				\
	bus_barrier((sc)->ste_res, reg, length, flags)

#define	STE_DESC_ALIGN		8
#define STE_RX_LIST_CNT		128
#define STE_TX_LIST_CNT		128
#define	STE_RX_LIST_SZ		\
	(sizeof(struct ste_desc_onefrag) * STE_RX_LIST_CNT)
#define	STE_TX_LIST_SZ		\
	(sizeof(struct ste_desc) * STE_TX_LIST_CNT)
#define	STE_ADDR_LO(x)		((uint64_t)(x) & 0xFFFFFFFF)
#define	STE_ADDR_HI(x)		((uint64_t)(x) >> 32)

/*
 * Since Tx status can hold up to 31 status bytes we should
 * check Tx status before controller fills it up. Otherwise
 * Tx MAC stalls.
 */
#define	STE_TX_INTR_FRAMES	16
#define	STE_TX_TIMEOUT		5
#define STE_TIMEOUT		1000
#define STE_MIN_FRAMELEN	60
#define STE_PACKET_SIZE		1536
#define STE_INC(x, y)		(x) = (x + 1) % y
#define STE_DEC(x, y)		(x) = ((x) + ((y) - 1)) % (y)
#define STE_NEXT(x, y)		(x + 1) % y

struct ste_type {
	uint16_t		ste_vid;
	uint16_t		ste_did;
	const char		*ste_name;
};

struct ste_list_data {
	struct ste_desc_onefrag	*ste_rx_list;
	bus_addr_t		ste_rx_list_paddr;
	struct ste_desc		*ste_tx_list;
	bus_addr_t		ste_tx_list_paddr;
};

struct ste_chain {
	struct ste_desc		*ste_ptr;
	struct mbuf		*ste_mbuf;
	struct ste_chain	*ste_next;
	uint32_t		ste_phys;
	bus_dmamap_t		ste_map;
};

struct ste_chain_onefrag {
	struct ste_desc_onefrag	*ste_ptr;
	struct mbuf		*ste_mbuf;
	struct ste_chain_onefrag	*ste_next;
	bus_dmamap_t		ste_map;
};

struct ste_chain_data {
	bus_dma_tag_t		ste_parent_tag;
	bus_dma_tag_t		ste_rx_tag;
	bus_dma_tag_t		ste_tx_tag;
	bus_dma_tag_t		ste_rx_list_tag;
	bus_dmamap_t		ste_rx_list_map;
	bus_dma_tag_t		ste_tx_list_tag;
	bus_dmamap_t		ste_tx_list_map;
	bus_dmamap_t		ste_rx_sparemap;
	struct ste_chain_onefrag ste_rx_chain[STE_RX_LIST_CNT];
	struct ste_chain	ste_tx_chain[STE_TX_LIST_CNT];
	struct ste_chain_onefrag *ste_rx_head;
	struct ste_chain	*ste_last_tx;
	int			ste_tx_prod;
	int			ste_tx_cons;
	int			ste_tx_cnt;
};

struct ste_softc {
	struct ifnet		*ste_ifp;
	struct resource		*ste_res;
	int			ste_res_id;
	int			ste_res_type;
	struct resource		*ste_irq;
	void			*ste_intrhand;
	struct ste_type		*ste_info;
	device_t		ste_miibus;
	device_t		ste_dev;
	int			ste_tx_thresh;
	int			ste_flags;
#define	STE_FLAG_ONE_PHY	0x0001
#define	STE_FLAG_LINK		0x8000
	int			ste_if_flags;
	int			ste_timer;
	int			ste_int_rx_act;
	int			ste_int_rx_mod;
	struct ste_list_data	ste_ldata;
	struct ste_chain_data	ste_cdata;
	struct callout		ste_callout;
	struct ste_hw_stats	ste_stats;
	struct mtx		ste_mtx;
};

#define	STE_LOCK(_sc)		mtx_lock(&(_sc)->ste_mtx)
#define	STE_UNLOCK(_sc)		mtx_unlock(&(_sc)->ste_mtx)
#define	STE_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->ste_mtx, MA_OWNED)
