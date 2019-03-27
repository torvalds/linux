/*-
 * Copyright (C) 2007 
 *	Oleksandr Tymoshenko <gonzo@freebsd.org>. All rights reserved.
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
 * THIS SOFTWFV IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE FV DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWFV, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __IF_FVREG_H__
#define	__IF_FVREG_H__

struct fv_desc {
	uint32_t	fv_stat;
	uint32_t	fv_devcs;
	uint32_t	fv_addr;
	uint32_t	fv_link;
};

#define	FV_DMASIZE(len)		((len)  & ((1 << 11)-1))		
#define	FV_PKTSIZE(len)		((len & 0xffff0000) >> 16)

#define	FV_RX_RING_CNT		128
#define	FV_TX_RING_CNT		128
#define	FV_TX_RING_SIZE		sizeof(struct fv_desc) * FV_TX_RING_CNT
#define	FV_RX_RING_SIZE		sizeof(struct fv_desc) * FV_RX_RING_CNT
#define	FV_RING_ALIGN		sizeof(struct fv_desc)
#define	FV_RX_ALIGN		sizeof(uint32_t)
#define	FV_MAXFRAGS		8
#define	FV_TX_INTR_THRESH	8

#define	FV_TX_RING_ADDR(sc, i)	\
    ((sc)->fv_rdata.fv_tx_ring_paddr + sizeof(struct fv_desc) * (i))
#define	FV_RX_RING_ADDR(sc, i)	\
    ((sc)->fv_rdata.fv_rx_ring_paddr + sizeof(struct fv_desc) * (i))
#define	FV_INC(x,y)		(x) = (((x) + 1) % y)

struct fv_txdesc {
	struct mbuf	*tx_m;
	bus_dmamap_t	tx_dmamap;
};

struct fv_rxdesc {
	struct mbuf	*rx_m;
	bus_dmamap_t	rx_dmamap;
	struct fv_desc	*desc;
	/* Use this values on error instead of allocating new mbuf */
	uint32_t	saved_ctl, saved_ca; 
};

struct fv_chain_data {
	bus_dma_tag_t		fv_parent_tag;
	bus_dma_tag_t		fv_tx_tag;
	struct fv_txdesc	fv_txdesc[FV_TX_RING_CNT];
	bus_dma_tag_t		fv_rx_tag;
	struct fv_rxdesc	fv_rxdesc[FV_RX_RING_CNT];
	bus_dma_tag_t		fv_tx_ring_tag;
	bus_dma_tag_t		fv_rx_ring_tag;
	bus_dmamap_t		fv_tx_ring_map;
	bus_dmamap_t		fv_rx_ring_map;
	bus_dmamap_t		fv_rx_sparemap;
	int			fv_tx_pkts;
	int			fv_tx_prod;
	int			fv_tx_cons;
	int			fv_tx_cnt;
	int			fv_rx_cons;

	bus_dma_tag_t		fv_sf_tag;
	bus_dmamap_t		fv_sf_buff_map;
	uint32_t		*fv_sf_buff;
};

struct fv_ring_data {
	struct fv_desc		*fv_rx_ring;
	struct fv_desc		*fv_tx_ring;
	bus_addr_t		fv_rx_ring_paddr;
	bus_addr_t		fv_tx_ring_paddr;
	bus_addr_t		fv_sf_paddr;
};

struct fv_softc {
	struct ifnet		*fv_ifp;	/* interface info */
	bus_space_handle_t	fv_bhandle;	/* bus space handle */
	bus_space_tag_t		fv_btag;	/* bus space tag */
	device_t		fv_dev;
	uint8_t			fv_eaddr[ETHER_ADDR_LEN];
	struct resource		*fv_res;
	int			fv_rid;
	struct resource		*fv_irq;
	void			*fv_intrhand;
	u_int32_t		sc_inten;	/* copy of CSR_INTEN */
	u_int32_t		sc_rxint_mask;	/* mask of Rx interrupts we want */
	u_int32_t		sc_txint_mask;	/* mask of Tx interrupts we want */
#ifdef MII
	device_t		fv_miibus;
#else
	struct ifmedia		fv_ifmedia;
#endif
#ifdef FV_MDIO
	device_t		fv_miiproxy;
#endif
	int			fv_if_flags;
	bus_dma_tag_t		fv_parent_tag;
	bus_dma_tag_t		fv_tag;
	struct mtx		fv_mtx;
	phandle_t		fv_ofw;
	struct callout		fv_stat_callout;
	struct task		fv_link_task;
	struct fv_chain_data	fv_cdata;
	struct fv_ring_data	fv_rdata;
	int			fv_link_status;
	int			fv_detach;
};

#define	FV_LOCK(_sc)		mtx_lock(&(_sc)->fv_mtx)
#define	FV_UNLOCK(_sc)		mtx_unlock(&(_sc)->fv_mtx)
#define	FV_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->fv_mtx, MA_OWNED)

/*
 * register space access macros
 */
#define	CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4(sc->fv_btag, sc->fv_bhandle, reg, val)

#define	CSR_READ_4(sc, reg)		\
	bus_space_read_4(sc->fv_btag, sc->fv_bhandle, reg)


/*	$NetBSD: aereg.h,v 1.2 2008/04/28 20:23:28 martin Exp $	*/

/*-
 * Copyright (c) 1999, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWFV IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE FV DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWFV, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Descriptor Status bits common to transmit and receive.
 */
#define	ADSTAT_OWN	0x80000000	/* Tulip owns descriptor */
#define	ADSTAT_ES	0x00008000	/* Error Summary */

/*
 * Descriptor Status bits for Receive Descriptor.
 */
#define	ADSTAT_Rx_FF	0x40000000	/* Filtering Fail */
#define	ADSTAT_Rx_FL	0x3fff0000	/* Frame Length including CRC */
#define	ADSTAT_Rx_DE	0x00004000	/* Descriptor Error */
#define	ADSTAT_Rx_LE	0x00001000	/* Length Error */
#define	ADSTAT_Rx_RF	0x00000800	/* Runt Frame */
#define	ADSTAT_Rx_MF	0x00000400	/* Multicast Frame */
#define	ADSTAT_Rx_FS	0x00000200	/* First Descriptor */
#define	ADSTAT_Rx_LS	0x00000100	/* Last Descriptor */
#define	ADSTAT_Rx_TL	0x00000080	/* Frame Too Long */
#define	ADSTAT_Rx_CS	0x00000040	/* Collision Seen */
#define	ADSTAT_Rx_RT	0x00000020	/* Frame Type */
#define	ADSTAT_Rx_RW	0x00000010	/* Receive Watchdog */
#define	ADSTAT_Rx_RE	0x00000008	/* Report on MII Error */
#define	ADSTAT_Rx_DB	0x00000004	/* Dribbling Bit */
#define	ADSTAT_Rx_CE	0x00000002	/* CRC Error */
#define	ADSTAT_Rx_ZER	0x00000001	/* Zero (always 0) */

#define	ADSTAT_Rx_LENGTH(x)	(((x) & ADSTAT_Rx_FL) >> 16)

/*
 * Descriptor Status bits for Transmit Descriptor.
 */
#define	ADSTAT_Tx_ES	0x00008000	/* Error Summary */
#define	ADSTAT_Tx_TO	0x00004000	/* Transmit Jabber Timeout */
#define	ADSTAT_Tx_LO	0x00000800	/* Loss of Carrier */
#define	ADSTAT_Tx_NC	0x00000400	/* No Carrier */
#define	ADSTAT_Tx_LC	0x00000200	/* Late Collision */
#define	ADSTAT_Tx_EC	0x00000100	/* Excessive Collisions */
#define	ADSTAT_Tx_HF	0x00000080	/* Heartbeat Fail */
#define	ADSTAT_Tx_CC	0x00000078	/* Collision Count */
#define	ADSTAT_Tx_ED	0x00000004	/* Excessive Deferral */
#define	ADSTAT_Tx_UF	0x00000002	/* Underflow Error */
#define	ADSTAT_Tx_DE	0x00000001	/* Deferred */

#define	ADSTAT_Tx_COLLISIONS(x)	(((x) & ADSTAT_Tx_CC) >> 3)

/*
 * Descriptor Control bits common to transmit and receive.
 */
#define	ADCTL_SIZE1	0x000007ff	/* Size of buffer 1 */
#define	ADCTL_SIZE1_SHIFT 0

#define	ADCTL_SIZE2	0x003ff800	/* Size of buffer 2 */
#define	ADCTL_SIZE2_SHIFT 11

#define	ADCTL_ER	0x02000000	/* End of Ring */
#define	ADCTL_CH	0x01000000	/* Second Address Chained */

/*
 * Descriptor Control bits for Transmit Descriptor.
 */
#define	ADCTL_Tx_IC	0x80000000	/* Interrupt on Completion */
#define	ADCTL_Tx_LS	0x40000000	/* Last Segment */
#define	ADCTL_Tx_FS	0x20000000	/* First Segment */
#define	ADCTL_Tx_SETUP	0x08000000	/* Setup frame */
#define	ADCTL_Tx_AC	0x04000000	/* Add CRC Disable */
#define	ADCTL_Tx_DPD	0x00800000	/* Disabled Padding */

/*
 * Control registers.
 */

/* tese are registers only found on this part */
#ifdef NOTUSE
#define	CSR_MACCTL	0x0000		/* mac control */
#define	CSR_MACHI	0x0004
#define	CSR_MACLO	0x0008
#define	CSR_HTHI	0x000C		/* multicast table high */
#define	CSR_HTLO	0x0010		/* multicast table low */
#define	CSR_MIIADDR	0x0014		/* mii address */
#define	CSR_MIIDATA	0x0018		/* mii data */
#define	CSR_FLOWC	0x001C		/* flow control */
#define	CSR_VL1		0x0020		/* vlan 1 tag */
#endif
	
/* these are more or less normal Tulip registers */
#define	CSR_BUSMODE	(0x08*0)	/* bus mode */
#define	CSR_TXPOLL	(0x08*1)	/* tx poll demand */
#define	CSR_RXPOLL	(0x08*2)	/* rx poll demand */
#define	CSR_RXLIST	(0x08*3)	/* rx base descriptor address */
#define	CSR_TXLIST	(0x08*4)	/* tx base descriptor address */
#define	CSR_STATUS	(0x08*5)	/* (interrupt) status */
#define	CSR_OPMODE	(0x08*6)	/* operation mode */
#define	CSR_INTEN	(0x08*7)	/* interrupt enable */
#define	CSR_MISSED	(0x08*8)	/* missed frame counter */

#ifdef NOTUSE
#define	CSR_HTBA	0x1050		/* host tx buffer address (ro) */
#define	CSR_HRBA	0x1054		/* host rx buffer address (ro) */
#endif

#define	CSR_MIIMNG	(0x08*9)	/* MII Management Register */
#define	CSR_FULLDUP	(0x08*11)	/* Full Duplex Register */

/* 21143 like register */
#define	FULLDUP_CS		0x80000000	/* Cycle Size */
#define	FULLDUP_TT_SHIFT	27	/* Transmit Timer */
#define	FULLDUP_NTP_SHIFT	24	/* Number of Transmit Packets */
#define	FULLDUP_RT_SHIFT	20	/* Receive Timer */
#define	FULLDUP_NRP_SHIFT	17	/* Number of Receive Packets */
#define	FULLDUP_CON_MODE	0x00010000	/* Continuous Mode */
#define	FULLDUP_TIM_SHIFT	0	/* Timer Value */

/* CSR_MACCTL - Mac Control */
#define	MACCTL_RE		0x00000004	/* rx enable */
#define	MACCTL_TE		0x00000008	/* tx enable */
#define	MACCTL_DC		0x00000020	/* deferral check */
#define	MACCTL_PSTR		0x00000100	/* automatic pad strip */
#define	MACCTL_DTRY		0x00000400	/* disable retry */
#define	MACCTL_DBF		0x00000800	/* disable broadcast frames */
#define	MACCTL_LCC		0x00001000	/* late collision control */
#define	MACCTL_HASH		0x00002000	/* hash filtering enable */
#define	MACCTL_HO		0x00008000	/* disable perfect filtering */
#define	MACCTL_PB		0x00010000	/* pass bad frames */
#define	MACCTL_IF		0x00020000	/* inverse filtering */
#define	MACCTL_PR		0x00040000	/* promiscuous mode */
#define	MACCTL_PM		0x00080000	/* pass all multicast */
#define	MACCTL_FDX		0x00100000	/* full duplex mode */
#define	MACCTL_LOOP		0x00600000	/* loopback mask */
#define	MACCTL_LOOP_INT		0x00200000	/* internal loopback */
#define	MACCTL_LOOP_EXT		0x00400000	/* external loopback */
#define	MACCTL_LOOP_NONE	0x00000000
#define	MACCTL_DRO		0x00800000	/* disable receive own */
#define	MACCTL_PS		0x08000000	/* port select, 0 = mii */
#define	MACCTL_HBD		0x10000000	/* heartbeat disable */
#define	MACCTL_BLE		0x40000000	/* mac big endian */
#define	MACCTL_RA		0x80000000	/* receive all packets */

/* CSR_MIIADDR - MII Addess */
#define	MIIADDR_BUSY		0x00000001	/* mii busy */
#define	MIIADDR_WRITE		0x00000002	/* mii write */
#define	MIIADDR_REG_MASK	0x000007C0	/* mii register */
#define	MIIADDR_REG_SHIFT	6
#define	MIIADDR_PHY_MASK	0x0000F800	/* mii phy */
#define	MIIADDR_PHY_SHIFT	11

#define	MIIADDR_GETREG(x)	(((x) & MIIADDR_REG) >> 6)
#define	MIIADDR_PUTREG(x)	(((x) << 6) & MIIADR_REG)
#define	MIIADDR_GETPHY(x)	(((x) & MIIADDR_PHY) >> 11)
#define	MIIADDR_PUTPHY(x)	(((x) << 6) & MIIADR_PHY)

/* CSR_FLOWC - Flow Control */
#define	FLOWC_FCB		0x00000001	/* flow control busy */
#define	FLOWC_FCE		0x00000002	/* flow control enable */
#define	FLOWC_PCF		0x00000004	/* pass control frames */
#define	FLOWC_PT		0xffff0000	/* pause time */

/* CSR_BUSMODE - Bus Mode */
#define	BUSMODE_SWR		0x00000001	/* software reset */
#define	BUSMODE_BAR		0x00000002	/* bus arbitration */
#define	BUSMODE_DSL		0x0000007c	/* descriptor skip length */
#define	BUSMODE_BLE		0x00000080	/* data buf endian */
						/* programmable burst length */
#define	BUSMODE_PBL_DEFAULT	0x00000000	/*     default value */
#define	BUSMODE_PBL_1LW		0x00000100	/*     1 longword */
#define	BUSMODE_PBL_2LW		0x00000200	/*     2 longwords */
#define	BUSMODE_PBL_4LW		0x00000400	/*     4 longwords */
#define	BUSMODE_PBL_8LW		0x00000800	/*     8 longwords */
#define	BUSMODE_PBL_16LW	0x00001000	/*    16 longwords */
#define	BUSMODE_PBL_32LW	0x00002000	/*    32 longwords */
#define	BUSMODE_TAP_SHIFT	17		/* Transmit Automatic Polling */
#define	BUSMODE_DBO		0x00100000	/* descriptor endian */
#define	BUSMODE_ALIGN_16B	0x01000000	/* force oddhw rx buf align */

/* CSR_TXPOLL - Transmit Poll Demand */
#define	TXPOLL_TPD		0x00000001	/* transmit poll demand */


/* CSR_RXPOLL - Receive Poll Demand */
#define	RXPOLL_RPD		0x00000001	/* receive poll demand */

/* CSR_STATUS - Status */
#define	STATUS_TI		0x00000001	/* transmit interrupt */
#define	STATUS_TPS		0x00000002	/* transmit process stopped */
#define	STATUS_TU		0x00000004	/* transmit buffer unavail */
#define	STATUS_TJT		0x00000008	/* transmit jabber timeout */
#define	STATUS_UNF		0x00000020	/* transmit underflow */
#define	STATUS_RI		0x00000040	/* receive interrupt */
#define	STATUS_RU		0x00000080	/* receive buffer unavail */
#define	STATUS_RPS		0x00000100	/* receive process stopped */
#define	STATUS_ETI		0x00000400	/* early transmit interrupt */
#define	STATUS_SE		0x00002000	/* system error */
#define	STATUS_ER		0x00004000	/* early receive (21041) */
#define	STATUS_AIS		0x00008000	/* abnormal intr summary */
#define	STATUS_NIS		0x00010000	/* normal interrupt summary */
#define	STATUS_RS		0x000e0000	/* receive process state */
#define	STATUS_RS_STOPPED	0x00000000	/* Stopped */
#define	STATUS_RS_FETCH		0x00020000	/* Running - fetch receive
						   descriptor */
#define	STATUS_RS_CHECK		0x00040000	/* Running - check for end
						   of receive */
#define	STATUS_RS_WAIT		0x00060000	/* Running - wait for packet */
#define	STATUS_RS_SUSPENDED	0x00080000	/* Suspended */
#define	STATUS_RS_CLOSE		0x000a0000	/* Running - close receive
						   descriptor */
#define	STATUS_RS_FLUSH		0x000c0000	/* Running - flush current
						   frame from FIFO */
#define	STATUS_RS_QUEUE		0x000e0000	/* Running - queue current
						   frame from FIFO into
						   buffer */
#define	STATUS_TS		0x00700000	/* transmit process state */
#define	STATUS_TS_STOPPED	0x00000000	/* Stopped */
#define	STATUS_TS_FETCH		0x00100000	/* Running - fetch transmit
						   descriptor */
#define	STATUS_TS_WAIT		0x00200000	/* Running - wait for end
						   of transmission */
#define	STATUS_TS_READING	0x00300000	/* Running - read buffer from
						   memory and queue into
						   FIFO */
#define	STATUS_TS_SUSPENDED	0x00600000	/* Suspended */
#define	STATUS_TS_CLOSE		0x00700000	/* Running - close transmit
						   descriptor */
#define	STATUS_TX_ABORT		0x00800000	/* Transmit bus abort */
#define	STATUS_RX_ABORT		0x01000000	/* Transmit bus abort */

/* CSR_OPMODE - Operation Mode */
#define	OPMODE_SR		0x00000002	/* start receive */
#define	OPMODE_OSF		0x00000004	/* operate on second frame */
#define	OPMODE_PR		0x00000040	/* promiscuous mode */
#define	OPMODE_PM		0x00000080	/* pass all multicast */
#define	OPMODE_FDX		0x00000200	/* full duplex mode */
#define	OPMODE_ST		0x00002000	/* start transmitter */
#define	OPMODE_TR		0x0000c000	/* threshold control */
#define	OPMODE_TR_32		0x00000000	/*     32 words */
#define	OPMODE_TR_64		0x00004000	/*     64 words */
#define	OPMODE_TR_128		0x00008000	/*    128 words */
#define	OPMODE_TR_256		0x0000c000	/*    256 words */
#define	OPMODE_SF		0x00200000	/* store and forward mode */
#define	OPMODE_SPEED		0x80000000	/* speed 100M:1 10M:0 */

/* CSR_INTEN - Interrupt Enable */
	/* See bits for CSR_STATUS -- Status */


/* CSR_MISSED - Missed Frames */
#define	MISSED_MFC		0xffff0000	/* missed packet count */
#define	MISSED_FOC		0x0000ffff	/* fifo overflow counter */

#define	MISSED_GETMFC(x)	((x) & MISSED_MFC)
#define	MISSED_GETFOC(x)	(((x) & MISSED_FOC) >> 16)

/* setup frame code refer dc code */

#define	FV_SFRAME_LEN		192
#define	FV_MIN_FRAMELEN		60

/*
 * MII Definitions for the 21041 and 21140/21140A/21142
 * copy from if_devar.h
 */
#define	MII_PREAMBLE            (~0)
#define	MII_TEST                0xAAAAAAAA
#define	MII_RDCMD               0x06
#define	MII_WRCMD               0x05
#define	MII_DIN                 0x00080000
#define	MII_RD                  0x00040000
#define	MII_WR                  0x00000000
#define	MII_DOUT                0x00020000
#define	MII_CLK                 0x00010000
#define	MII_CLKON               MII_CLK
#define	MII_CLKOFF              MII_CLK

#endif /* __IF_FVREG_H__ */
