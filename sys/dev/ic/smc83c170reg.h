/*	$OpenBSD: smc83c170reg.h,v 1.3 2022/01/09 05:42:42 jsg Exp $	*/
/*	$NetBSD: smc83c170reg.h,v 1.9 2003/11/08 16:08:13 tsutsui Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_IC_SMC83C170REG_H_
#define	_DEV_IC_SMC83C170REG_H_

/*
 * Register description for the Standard Microsystems Corp. 83C170
 * Ethernet PCI Integrated Controller (EPIC/100).
 */

/*
 * EPIC transmit descriptor.  Must be 4-byte aligned.
 */
struct epic_txdesc {
	u_int32_t	et_txstatus;	/* transmit status; see below */
	u_int32_t	et_bufaddr;	/* buffer address */
	u_int32_t	et_control;	/* control word; see below */
	u_int32_t	et_nextdesc;	/* next descriptor pointer */
};

/* et_txstatus */
#define	TXSTAT_TXLENGTH_SHIFT	16	/* TX length in higher 16bits */
#define	TXSTAT_TXLENGTH(x)	((x) << TXSTAT_TXLENGTH_SHIFT)

#define	ET_TXSTAT_OWNER		0x8000	/* NIC owns descriptor */
#define	ET_TXSTAT_COLLMASK	0x1f00	/* collisions */
#define	ET_TXSTAT_DEFERRING	0x0080	/* deferring due to jabber */
#define	ET_TXSTAT_OOWCOLL	0x0040	/* out of window collision */
#define	ET_TXSTAT_CDHB		0x0020	/* collision detect heartbeat */
#define	ET_TXSTAT_UNDERRUN	0x0010	/* DMA underrun */
#define	ET_TXSTAT_CARSENSELOST	0x0008	/* carrier lost */
#define	ET_TXSTAT_TXWITHCOLL	0x0004	/* encountered collisions during tx */
#define	ET_TXSTAT_NONDEFERRED	0x0002	/* transmitted without deferring */
#define	ET_TXSTAT_PACKETTX	0x0001	/* packet transmitted successfully */

#define	TXSTAT_COLLISIONS(x)	(((x) & ET_TXSTAT_COLLMASK) >> 8)

/* et_control */
#define	TXCTL_BUFLENGTH_MASK	0x0000ffff /* buf length in lower 16bits */
#define	TXCTL_BUFLENGTH(x)	((x) & TXCTL_BUFLENGTH_MASK)

#define	ET_TXCTL_LASTDESC	0x00100000 /* last descriptor in frame */
#define	ET_TXCTL_NOCRC		0x00080000 /* disable CRC generation */
#define	ET_TXCTL_IAF		0x00040000 /* interrupt after frame */
#define	ET_TXCTL_LFFORM		0x00020000 /* alternate fraglist format */
#define	ET_TXCTL_FRAGLIST	0x00010000 /* descriptor points to fraglist */

/*
 * EPIC receive descriptor.  Must be 4-byte aligned.
 */
struct epic_rxdesc {
	u_int32_t	er_rxstatus;	/* receive status; see below */
	u_int32_t	er_bufaddr;	/* buffer address */
	u_int32_t	er_control;	/* control word; see below */
	u_int32_t	er_nextdesc;	/* next descriptor pointer */
};

/* er_rxstatus */
#define	RXSTAT_RXLENGTH_SHIFT	16	/* TX length in higher 16bits */
#define	RXSTAT_RXLENGTH(x)	((x) >> RXSTAT_RXLENGTH_SHIFT)

#define	ER_RXSTAT_OWNER		0x8000	/* NIC owns descriptor */
#define	ER_RXSTAT_HDRCOPIED	0x4000	/* rx status posted after hdr copy */
#define	ER_RXSTAT_FRAGLISTERR	0x2000	/* ran out of frags to copy frame */
#define	ER_RXSTAT_NETSTATVALID	0x1000	/* length and status are valid */
#define	ER_RXSTAT_RCVRDIS	0x0040	/* receiver disabled */
#define	ER_RXSTAT_BCAST		0x0020	/* broadcast address recognized */
#define	ER_RXSTAT_MCAST		0x0010	/* multicast address recognized */
#define	ER_RXSTAT_MISSEDPKT	0x0008	/* missed packet */
#define	ER_RXSTAT_CRCERROR	0x0004	/* EPIC or MII asserted CRC error */
#define	ER_RXSTAT_ALIGNERROR	0x0002	/* frame not byte-aligned */
#define	ER_RXSTAT_PKTINTACT	0x0001	/* packet received without error */

/* er_control */
#define	RXCTL_BUFLENGTH_MASK	0x0000ffff /* buf length in lower 16bits */
#define	RXCTL_BUFLENGTH(x)	((x) & RXCTL_BUFLENGTH_MASK)

#define	ER_RXCTL_HEADER		0x00040000 /* descriptor is for hdr copy */
#define	ER_RXCTL_LFFORM		0x00020000 /* alternate fraglist format */
#define	ER_RXCTL_FRAGLIST	0x00010000 /* descriptor points to fraglist */

/*
 * This is not really part of the register description, but we need
 * to define the number of transmit fragments *somewhere*.
 */
#define	EPIC_NFRAGS		16	/* maximum number of frags in list */

/*
 * EPIC fraglist descriptor.
 */
struct epic_fraglist {
	u_int32_t	ef_nfrags;	/* number of frags in list */
	struct {
		u_int32_t ef_addr;	/* address of frag */
		u_int32_t ef_length;	/* length of frag */
	} ef_frags[EPIC_NFRAGS];
};

/*
 * EPIC control registers.
 */

#define	EPIC_COMMAND		0x00 /* COMMAND */
#define	COMMAND_TXUGO		0x00000080	/* start tx after underrun */
#define	COMMAND_STOP_RDMA	0x00000040	/* stop rx dma */
#define	COMMAND_STOP_TDMA	0x00000020	/* stop tx dma */
#define	COMMAND_NEXTFRAME	0x00000010	/* move onto next rx frame */
#define	COMMAND_RXQUEUED	0x00000008	/* queue a rx descriptor */
#define	COMMAND_TXQUEUED	0x00000004	/* queue a tx descriptor */
#define	COMMAND_START_RX	0x00000002	/* start receiver */
#define	COMMAND_STOP_RX		0x00000001	/* stop receiver */

#define	EPIC_INTSTAT		0x04 /* INTERRUPT STATUS */
#define	INTSTAT_PTA		0x08000000	/* PCI target abort */
#define	INTSTAT_PMA		0x04000000	/* PCI master abort */
#define	INTSTAT_APE		0x02000000	/* PCI address parity error */
#define	INTSTAT_DPE		0x01000000	/* PCI data parity error */
#define	INTSTAT_RSV		0x00800000	/* rx status valid */
#define	INTSTAT_RCTS		0x00400000	/* rx copy threshold status */
#define	INTSTAT_RBE		0x00200000	/* rx buffers empty */
#define	INTSTAT_TCIP		0x00100000	/* tx copy in progress */
#define	INTSTAT_RCIP		0x00080000	/* rx copy in progress */
#define	INTSTAT_TXIDLE		0x00040000	/* transmit idle */
#define	INTSTAT_RXIDLE		0x00020000	/* receive idle */
#define	INTSTAT_INT_ACTV	0x00010000	/* interrupt active */
#define	INTSTAT_GP2_INT		0x00008000	/* gpio2 low (PHY event) */
#define	INTSTAT_FATAL_INT	0x00001000	/* fatal error occurred */
#define	INTSTAT_RCT		0x00000800	/* rx copy threshold crossed */
#define	INTSTAT_PREI		0x00000400	/* preemptive interrupt */
#define	INTSTAT_CNT		0x00000200	/* counter overflow */
#define	INTSTAT_TXU		0x00000100	/* transmit underrun */
#define	INTSTAT_TQE		0x00000080	/* transmit queue empty */
#define	INTSTAT_TCC		0x00000040	/* transmit chain complete */
#define	INTSTAT_TXC		0x00000020	/* transmit complete */
#define	INTSTAT_RXE		0x00000010	/* receive error */
#define	INTSTAT_OVW		0x00000008	/* rx buffer overflow */
#define	INTSTAT_RQE		0x00000004	/* receive queue empty */
#define	INTSTAT_HCC		0x00000002	/* header copy complete */
#define	INTSTAT_RCC		0x00000001	/* receive copy complete */

#define	EPIC_INTMASK		0x08 /* INTERRUPT MASK */
	/* Bits 0-15 enable the corresponding interrupt in INTSTAT. */

#define	EPIC_GENCTL		0x0c /* GENERAL CONTROL */
#define	GENCTL_RESET_PHY	0x00004000	/* reset PHY */
#define	GENCTL_SOFT1		0x00002000	/* software use */
#define	GENCTL_SOFT0		0x00001000	/* software use */
#define	GENCTL_MEM_READ_CTL1	0x00000800	/* PCI memory control */
#define	GENCTL_MEM_READ_CTL0	0x00000400	/* (see below) */
#define	GENCTL_RX_FIFO_THRESH1	0x00000200	/* rx fifo thresh */
#define	GENCTL_RX_FIFO_THRESH0	0x00000100	/* (see below) */
#define	GENCTL_BIG_ENDIAN	0x00000020	/* big endian mode */
#define	GENCTL_ONECOPY		0x00000010	/* auto-NEXTFRAME */
#define	GENCTL_POWERDOWN	0x00000008	/* powersave sleep mode */
#define	GENCTL_SOFTINT		0x00000004	/* software-generated intr */
#define	GENCTL_INTENA		0x00000002	/* interrupt enable */
#define	GENCTL_SOFTRESET	0x00000001	/* initialize EPIC */

/*
 * Explanation of MEMORY READ CONTROL:
 *
 * These bits control which PCI command the transmit DMA will use when
 * bursting data over the PCI bus.  When CTL1 is set, the transmit DMA
 * will use the PCI "memory read line" command.  When CTL0 is set, the
 * transmit DMA will use the PCI "memory read multiple" command.  When
 * neither bit is set, the transmit DMA will use the "memory read" command.
 * Use of "memory read line" or "memory read multiple" may enhance
 * performance on some systems.
 */

/*
 * Explanation of RECEIVE FIFO THRESHOLD:
 *
 * Controls the level at which the PCI burst state machine begins to
 * empty the receive FIFO.  Default is "1/2 full" (0,1).
 *
 *	0,0	1/4 full	32 bytes
 *	0,1	1/2 full	64 bytes
 *	1,0	3/4 full	96 bytes
 *	1,1	full		128 bytes
 */

#define	EPIC_NVCTL		0x10 /* NON-VOLATILE CONTROL */
#define	NVCTL_IPG_DLY_MASK	0x00000780	/* interpacket delay gap */
#define	NVCTL_CB_MODE		0x00000040	/* CardBus mode */
#define	NVCTL_GPIO2		0x00000020	/* general purpose i/o */
#define	NVCTL_GPIO1		0x00000010	/* ... */
#define	NVCTL_GPOE2		0x00000008	/* general purpose output ena */
#define	NVCTL_GPOE1		0x00000004	/* ... */
#define	NVCTL_CLKRUNSUPP	0x00000002	/* clock run supported */
#define	NVCTL_ENAMEMMAP		0x00000001	/* enable memory map */

#define	NVCTL_IPG_DLY(x)	(((x) & NVCTL_IPG_DLY_MASK) >> 7)

#define	EPIC_EECTL		0x14 /* EEPROM CONTROL */
#define	EECTL_EEPROMSIZE	0x00000040	/* eeprom size; see below */
#define	EECTL_EERDY		0x00000020	/* eeprom ready */
#define	EECTL_EEDO		0x00000010	/* eeprom data out (from) */
#define	EECTL_EEDI		0x00000008	/* eeprom data in (to) */
#define	EECTL_EESK		0x00000004	/* eeprom clock */
#define	EECTL_EECS		0x00000002	/* eeprom chip select */
#define	EECTL_ENABLE		0x00000001	/* eeprom enable */

/*
 * Explanation of EEPROM SIZE:
 *
 * Indicates the size of the serial EEPROM:
 *
 *	1	16x16 or 64x16
 *	0	128x16 or 256x16
 */

/*
 * Serial EEPROM opcodes, including start bit:
 */
#define	EPIC_EEPROM_OPC_WRITE	0x05
#define	EPIC_EEPROM_OPC_READ	0x06

#define	EPIC_PBLCNT		0x18 /* PBLCNT */
#define	PBLCNT_MASK		0x0000003f	/* programmable burst length */

#define	EPIC_TEST		0x1c /* TEST */
#define	TEST_CLOCKTEST		0x00000008

#define	EPIC_CRCCNT		0x20 /* CRC ERROR COUNTER */
#define	CRCCNT_MASK		0x0000000f	/* crc errs since last read */

#define	EPIC_ALICNT		0x24 /* FRAME ALIGNMENT ERROR COUNTER */
#define	ALICNT_MASK		0x0000000f	/* align errs since last read */

#define	EPIC_MPCNT		0x28 /* MISSED PACKET COUNTER */
#define	MPCNT_MASK		0x0000000f	/* miss. pkts since last read */

#define	EPIC_RXFIFO		0x2c

#define	EPIC_MMCTL		0x30 /* MII MANAGEMENT INTERFACE CONTROL */
#define	MMCTL_PHY_ADDR_MASK	0x00003e00	/* phy address field */
#define	MMCTL_PHY_REG_ADDR_MASK	0x000001f0	/* phy register address field */
#define	MMCTL_RESPONDER		0x00000008	/* phy responder */
#define	MMCTL_WRITE		0x00000002	/* write to phy */
#define	MMCTL_READ		0x00000001	/* read from phy */

#define	MMCTL_ARG(phy, reg, cmd)	(((phy) << 9) | ((reg) << 4) | (cmd))

#define	EPIC_MMDATA		0x34 /* MII MANAGEMENT INTERFACE DATA */
#define	MMDATA_MASK		0x0000ffff	/* MII frame data */

#define	EPIC_MIICFG		0x38 /* MII CONFIGURATION */
#define	MIICFG_ALTDIR		0x00000080	/* alternate direction */
#define	MIICFG_ALTDATA		0x00000040	/* alternate data */
#define	MIICFG_ALTCLOCK		0x00000020	/* alternate clock source */
#define	MIICFG_ENASER		0x00000010	/* enable serial manag intf */
#define	MIICFG_PHYPRESENT	0x00000008	/* phy present on MII */
#define	MIICFG_LINKSTATUS	0x00000004	/* 694 link status */
#define	MIICFG_ENABLE		0x00000002	/* enable 694 */
#define	MIICFG_SERMODEENA	0x00000001	/* serial mode enable */

#define	EPIC_IPG		0x3c /* INTERPACKET GAP */
#define	IPG_INTERFRAME_MASK	0x00007f00	/* interframe gap time */
#define	IPG_INTERPKT_MASK	0x000000ff	/* interpacket gap time */

#define	EPIC_LAN0		0x40 /* LAN ADDRESS */

#define	EPIC_LAN1		0x44

#define	EPIC_LAN2		0x48

#define	LANn_MASK		0x0000ffff

/*
 * Explanation of LAN ADDRESS registers:
 *
 * LAN address is described as:
 *
 *	0000 [n1][n0][n3][n2] | 0000 [n5][n4][n7][n6] | 0000 [n9][n8][n11][n10]
 *
 * n == one nibble, mapped as follows:
 *
 *	LAN0	[15-12]		n3
 *	LAN0	[11-8]		n2
 *	LAN0	[7-4]		n1
 *	LAN0	[3-0]		n0
 *	LAN1	[15-12]		n7
 *	LAN1	[11-8]		n6
 *	LAN1	[7-4]		n5
 *	LAN1	[3-0]		n4
 *	LAN2	[15-12]		n11
 *	LAN2	[11-8]		n10
 *	LAN2	[7-4]		n9
 *	LAN2	[3-0]		n8
 *
 * The LAN address is automatically recalled from the EEPROM after a
 * hard reset.
 */

#define	EPIC_IDCHK		0x4c /* BOARD ID/CHECKSUM */
#define	IDCHK_ID_MASK		0x0000ff00	/* board ID */
#define	IDCHK_CKSUM_MASK	0x000000ff	/* checksum (should be 0xff) */

#define	EPIC_MC0		0x50 /* MULTICAST ADDRESS HASH TABLE */

#define	EPIC_MC1		0x54

#define	EPIC_MC2		0x58

#define	EPIC_MC3		0x5c

/*
 * Explanation of MULTICAST ADDRESS HASH TABLE registers:
 *
 * Bits in the hash table are encoded as follows:
 *
 *	MC0	[15-0]
 *	MC1	[31-16]
 *	MC2	[47-32]
 *	MC3	[53-48]
 */

#define	EPIC_RXCON		0x60 /* RECEIVE CONTROL */
#define	RXCON_EXTBUFSIZESEL1	0x00000200	/* ext buf size; see below */
#define	RXCON_EXTBUFSIZESEL0	0x00000100	/* ... */
#define	RXCON_EARLYRXENABLE	0x00000080	/* early receive enable */
#define	RXCON_MONITORMODE	0x00000040	/* monitor mode */
#define	RXCON_PROMISCMODE	0x00000020	/* promiscuous mode */
#define	RXCON_RXINVADDR		0x00000010	/* rx inv individual addr */
#define	RXCON_RXMULTICAST	0x00000008	/* receive multicast */
#define	RXCON_RXBROADCAST	0x00000004	/* receive broadcast */
#define	RXCON_RXRUNT		0x00000002	/* receive runt frames */
#define	RXCON_SAVEERRPKTS	0x00000001	/* save errored packets */

/*
 * Explanation of EXTERNAL BUFFER SIZE SELECT:
 *
 * 	0,0	external buffer access is disabled
 *	0,1	16k
 *	1,0	32k
 *	1,1	128k
 */

#define	EPIC_RXSTAT		0x64 /* RECEIVE STATUS */

#define	EPIC_RXCNT		0x68

#define	EPIC_RXTEST		0x6c

#define	EPIC_TXCON		0x70 /* TRANSMIT CONTROL */
#define	TXCON_SLOTTIME_MASK	0x000000f8	/* slot time */
#define	TXCON_LOOPBACK_D2	0x00000004	/* loopback mode bit 2 */
#define	TXCON_LOOPBACK_D1	0x00000002	/* loopback mode bit 1 */
#define	TXCON_EARLYTX_ENABLE	0x00000001	/* early transmit enable */

/*
 * Explanation of LOOPBACK MODE BIT:
 *
 *	0,0	normal operation
 *	0,1	internal loopback (before PHY)
 *	1,0	external loopback (after PHY)
 *	1,1	full duplex - decouples transmit and receive blocks
 */

#define	EPIC_TXSTAT		0x74 /* TRANSMIT STATUS */

#define	EPIC_TDPAR		0x78

#define	EPIC_TXTEST		0x7c

#define	EPIC_PRFDAR		0x80

#define	EPIC_PRCDAR		0x84 /* PCI RECEIVE CURRENT DESCRIPTOR ADDR */

#define	EPIC_PRHDAR		0x88

#define	EPIC_PRFLAR		0x8c

#define	EPIC_PRDLGTH		0x90

#define	EPIC_PRFCNT		0x94

#define	EPIC_PRLCAR		0x98

#define	EPIC_PRLPAR		0x9c

#define	EPIC_PREFAR		0xa0

#define	EPIC_PRSTAT		0xa4 /* PCI RECEIVE DMA STATUS */

#define	EPIC_PRBUF		0xa8

#define	EPIC_RDNCAR		0xac

#define	EPIC_PRCPTHR		0xb0 /* PCI RECEIVE COPY THRESHOLD */

#define	EPIC_ROMDATA		0xb4

#define	EPIC_PREEMPR		0xbc

#define	EPIC_PTFDAR		0xc0

#define	EPIC_PTCDAR		0xc4 /* PCI TRANSMIT CURRENT DESCRIPTOR ADDR */

#define	EPIC_PTHDAR		0xc8

#define	EPIC_PTFLAR		0xcc

#define	EPIC_PTDLGTH		0xd0

#define	EPIC_PTFCNT		0xd4

#define	EPIC_PTLCAR		0xd8

#define	EPIC_ETXTHR		0xdc /* EARLY TRANSMIT THRESHOLD */

#define	EPIC_PTETXC		0xe0

#define	EPIC_PTSTAT		0xe4

#define	EPIC_PTBUF		0xe8

#define	EPIC_PTFDAR2		0xec

#define	EPIC_FEVTR		0xf0 /* FEVTR (CardBus) */

#define	EPIC_FEVTRMSKR		0xf4 /* FEVTRMSKR (CardBus) */

#define	EPIC_FPRSTSTR		0xf8 /* FPRSTR (CardBus) */

#define	EPIC_FFRCEVTR		0xfc /* PPRCEVTR (CardBus) */

/*
 * EEPROM format:
 *
 *	Word	Bits	Description
 *	----	----	-----------
 *	0	7-0	LAN Address Byte 0
 *	0	15-8	LAN Address Byte 1
 *	1	7-0	LAN Address Byte 2
 *	1	15-8	LAN Address Byte 3
 *	2	7-0	LAN Address Byte 4
 *	2	15-8	LAN Address Byte 5
 *	3	7-0	Board ID
 *	3	15-8	Checksum
 *	4	5-0	Non-Volatile Control Register Contents
 *	5	7-0	PCI Minimum Grant Desired Setting
 *	5	15-8	PCI Maximum Latency Desired Setting
 *	6	15-0	Subsystem Vendor ID
 *	7	14-0	Subsystem ID
 */

#endif /* _DEV_IC_SMC83C170REG_H_ */
