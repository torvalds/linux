/*	$NetBSD: dc21040reg.h,v 1.15 1998/05/22 18:50:59 matt Exp $	*/

/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1994, 1995, 1996 Matt Thomas <matt@3am-software.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Id: dc21040reg.h,v 1.24 1997/05/16 19:47:09 thomas Exp
 */

#if !defined(_DC21040_H)
#define _DC21040_H

#if defined(BYTE_ORDER) && BYTE_ORDER == BIG_ENDIAN
#define	TULIP_BITFIELD2(a, b)		      b, a
#define	TULIP_BITFIELD3(a, b, c)	   c, b, a
#define	TULIP_BITFIELD4(a, b, c, d)	d, c, b, a
#else
#define	TULIP_BITFIELD2(a, b)		a, b
#define	TULIP_BITFIELD3(a, b, c)	a, b, c
#define	TULIP_BITFIELD4(a, b, c, d)	a, b, c, d
#endif

typedef struct {
    u_int32_t d_status;
    u_int32_t TULIP_BITFIELD3(d_length1 : 11,
			      d_length2 : 11,
			      d_flag : 10);
    u_int32_t d_addr1;
    u_int32_t d_addr2;
} tulip_desc_t;

#define	TULIP_DSTS_OWNER	0x80000000	/* Owner (1 = 21040) */
#define	TULIP_DSTS_ERRSUM	0x00008000	/* Error Summary */
/*
 * Transmit Status
 */
#define	TULIP_DSTS_TxBABBLE	0x00004000	/* Transmitter Babbled */
#define	TULIP_DSTS_TxCARRLOSS	0x00000800	/* Carrier Loss */
#define	TULIP_DSTS_TxNOCARR	0x00000400	/* No Carrier */
#define	TULIP_DSTS_TxLATECOLL	0x00000200	/* Late Collision */
#define	TULIP_DSTS_TxEXCCOLL	0x00000100	/* Excessive Collisions */
#define	TULIP_DSTS_TxNOHRTBT	0x00000080	/* No Heartbeat */
#define	TULIP_DSTS_TxCOLLMASK	0x00000078	/* Collision Count (mask) */
#define	TULIP_DSTS_V_TxCOLLCNT	0x00000003	/* Collision Count (bit) */
#define	TULIP_DSTS_TxLINKFAIL	0x00000004	/* Link Failure */
#define	TULIP_DSTS_TxUNDERFLOW	0x00000002	/* Underflow Error */
#define	TULIP_DSTS_TxDEFERRED	0x00000001	/* Initially Deferred */
/*
 * Receive Status
 */
#define	TULIP_DSTS_RxBADLENGTH	0x00004000	/* Length Error */
#define	TULIP_DSTS_RxDATATYPE	0x00003000	/* Data Type */
#define	TULIP_DSTS_RxRUNT	0x00000800	/* Runt Frame */
#define	TULIP_DSTS_RxMULTICAST	0x00000400	/* Multicast Frame */
#define	TULIP_DSTS_RxFIRSTDESC	0x00000200	/* First Descriptor */
#define	TULIP_DSTS_RxLASTDESC	0x00000100	/* Last Descriptor */
#define	TULIP_DSTS_RxTOOLONG	0x00000080	/* Frame Too Long */
#define	TULIP_DSTS_RxCOLLSEEN	0x00000040	/* Collision Seen */
#define	TULIP_DSTS_RxFRAMETYPE	0x00000020	/* Frame Type */
#define	TULIP_DSTS_RxWATCHDOG	0x00000010	/* Receive Watchdog */
#define	TULIP_DSTS_RxDRBBLBIT	0x00000004	/* Dribble Bit */
#define	TULIP_DSTS_RxBADCRC	0x00000002	/* CRC Error */
#define	TULIP_DSTS_RxOVERFLOW	0x00000001	/* Overflow */


#define	TULIP_DFLAG_ENDRING	0x0008		/* End of Transmit Ring */
#define	TULIP_DFLAG_CHAIN	0x0004		/* Chain using d_addr2 */

#define	TULIP_DFLAG_TxWANTINTR	0x0200		/* Signal Interrupt on Completion */
#define	TULIP_DFLAG_TxLASTSEG	0x0100		/* Last Segment */
#define	TULIP_DFLAG_TxFIRSTSEG	0x0080		/* First Segment */
#define	TULIP_DFLAG_TxINVRSFILT	0x0040		/* Inverse Filtering */
#define	TULIP_DFLAG_TxSETUPPKT	0x0020		/* Setup Packet */
#define	TULIP_DFLAG_TxHASCRC	0x0010		/* Don't Append the CRC */
#define	TULIP_DFLAG_TxNOPADDING	0x0002		/* Don't AutoPad */
#define	TULIP_DFLAG_TxHASHFILT	0x0001		/* Hash/Perfect Filtering */

/*
 * The 21040 Registers (IO Space Addresses)
 */
#define	TULIP_REG_BUSMODE	0x00	/* CSR0  -- Bus Mode */
#define	TULIP_REG_TXPOLL	0x08	/* CSR1  -- Transmit Poll Demand */
#define	TULIP_REG_RXPOLL	0x10	/* CSR2  -- Receive Poll Demand */
#define	TULIP_REG_RXLIST	0x18	/* CSR3  -- Receive List Base Addr */
#define	TULIP_REG_TXLIST	0x20	/* CSR4  -- Transmit List Base Addr */
#define	TULIP_REG_STATUS	0x28	/* CSR5  -- Status */
#define	TULIP_REG_CMD		0x30	/* CSR6  -- Command */
#define	TULIP_REG_INTR		0x38	/* CSR7  -- Interrupt Control */
#define	TULIP_REG_MISSES	0x40	/* CSR8  -- Missed Frame Counter */
#define	TULIP_REG_ADDRROM	0x48	/* CSR9  -- ENET ROM Register */
#define	TULIP_REG_RSRVD		0x50	/* CSR10 -- Reserved */
#define	TULIP_REG_FULL_DUPLEX	0x58	/* CSR11 -- Full Duplex */
#define	TULIP_REG_SIA_STATUS	0x60	/* CSR12 -- SIA Status */
#define	TULIP_REG_SIA_CONN	0x68	/* CSR13 -- SIA Connectivity */
#define	TULIP_REG_SIA_TXRX	0x70	/* CSR14 -- SIA Tx Rx */
#define	TULIP_REG_SIA_GEN	0x78	/* CSR15 -- SIA General */

/*
 * CSR5 -- Status Register
 * CSR7 -- Interrupt Control
 */
#define	TULIP_STS_ERRORMASK	0x03800000L		/* ( R)  Error Bits (Valid when SYSERROR is set) */
#define	TULIP_STS_ERR_PARITY	0x00000000L		/*        000 - Parity Error (Perform Reset) */
#define	TULIP_STS_ERR_MASTER	0x00800000L		/*        001 - Master Abort */
#define	TULIP_STS_ERR_TARGET	0x01000000L		/*        010 - Target Abort */
#define	TULIP_STS_ERR_SHIFT	23
#define	TULIP_STS_TXSTATEMASK	0x00700000L		/* ( R)  Transmission Process State */
#define	TULIP_STS_TXS_RESET	0x00000000L		/*        000 - Rset or transmit jabber expired */
#define	TULIP_STS_TXS_FETCH	0x00100000L		/*        001 - Fetching transmit descriptor */
#define	TULIP_STS_TXS_WAITEND	0x00200000L		/*        010 - Wait for end of transmission */
#define	TULIP_STS_TXS_READING	0x00300000L		/*        011 - Read buffer and enqueue data */
#define	TULIP_STS_TXS_RSRVD	0x00400000L		/*        100 - Reserved */
#define	TULIP_STS_TXS_SETUP	0x00500000L		/*        101 - Setup Packet */
#define	TULIP_STS_TXS_SUSPEND	0x00600000L		/*        110 - Transmit FIFO underflow or an
								  unavailable transmit descriptor */
#define	TULIP_STS_TXS_CLOSE	0x00700000L		/*        111 - Close transmit descriptor */
#define	TULIP_STS_RXSTATEMASK	0x000E0000L		/* ( R)  Receive Process State*/
#define	TULIP_STS_RXS_STOPPED	0x00000000L		/*        000 - Stopped */
#define	TULIP_STS_RXS_FETCH	0x00020000L		/*        001 - Running -- Fetch receive descriptor */
#define	TULIP_STS_RXS_ENDCHECK	0x00040000L		/*        010 - Running -- Check for end of receive
								  packet before prefetch of next descriptor */
#define	TULIP_STS_RXS_WAIT	0x00060000L		/*        011 - Running -- Wait for receive packet */
#define	TULIP_STS_RXS_SUSPEND	0x00080000L		/*        100 - Suspended -- As a result of
								  unavailable receive buffers */
#define	TULIP_STS_RXS_CLOSE	0x000A0000L		/*        101 - Running -- Close receive descriptor */
#define	TULIP_STS_RXS_FLUSH	0x000C0000L		/*        110 - Running -- Flush the current frame
								  from the receive FIFO as a result of
								  an unavailable receive buffer */
#define	TULIP_STS_RXS_DEQUEUE	0x000E0000L		/*        111 - Running -- Dequeue the receive frame
								  from the receive FIFO into the receive
								  buffer. */
#define	TULIP_STS_NORMALINTR	0x00010000L		/* (RW)  Normal Interrupt */
#define	TULIP_STS_ABNRMLINTR	0x00008000L		/* (RW)  Abnormal Interrupt */
#define	TULIP_STS_SYSERROR	0x00002000L		/* (RW)  System Error */
#define	TULIP_STS_LINKFAIL	0x00001000L		/* (RW)  Link Failure (21040) */
#define	TULIP_STS_FULDPLXSHRT	0x00000800L		/* (RW)  Full Duplex Short Fram Rcvd (21040) */
#define	TULIP_STS_GPTIMEOUT	0x00000800L		/* (RW)  General Purpose Timeout (21140) */
#define	TULIP_STS_AUI		0x00000400L		/* (RW)  AUI/TP Switch (21040) */
#define	TULIP_STS_RXTIMEOUT	0x00000200L		/* (RW)  Receive Watchbog Timeout */
#define	TULIP_STS_RXSTOPPED	0x00000100L		/* (RW)  Receive Process Stopped */
#define	TULIP_STS_RXNOBUF	0x00000080L		/* (RW)  Receive Buffer Unavailable */
#define	TULIP_STS_RXINTR	0x00000040L		/* (RW)  Receive Interrupt */
#define	TULIP_STS_TXUNDERFLOW	0x00000020L		/* (RW)  Transmit Underflow */
#define	TULIP_STS_LINKPASS	0x00000010L		/* (RW)  LinkPass (21041) */
#define	TULIP_STS_TXBABBLE	0x00000008L		/* (RW)  Transmit Jabber Timeout */
#define	TULIP_STS_TXNOBUF	0x00000004L		/* (RW)  Transmit Buffer Unavailable */
#define	TULIP_STS_TXSTOPPED	0x00000002L		/* (RW)  Transmit Process Stopped */
#define	TULIP_STS_TXINTR	0x00000001L		/* (RW)  Transmit Interrupt */

/*
 * CSR6 -- Command (Operation Mode) Register
 */
#define	TULIP_CMD_MUSTBEONE	0x02000000L		/* (RW)  Must Be One (21140) */
#define	TULIP_CMD_SCRAMBLER	0x01000000L		/* (RW)  Scrambler Mode (21140) */
#define	TULIP_CMD_PCSFUNCTION	0x00800000L		/* (RW)  PCS Function (21140) */
#define	TULIP_CMD_TXTHRSHLDCTL	0x00400000L		/* (RW)  Transmit Threshold Mode (21140) */
#define	TULIP_CMD_STOREFWD	0x00200000L		/* (RW)  Store and Forward (21140) */
#define	TULIP_CMD_NOHEARTBEAT	0x00080000L		/* (RW)  No Heartbeat (21140) */
#define	TULIP_CMD_PORTSELECT	0x00040000L		/* (RW)  Post Select (100Mb) (21140) */
#define	TULIP_CMD_ENHCAPTEFFCT	0x00040000L		/* (RW)  Enhanced Capture Effecty (21041) */
#define	TULIP_CMD_CAPTREFFCT	0x00020000L		/* (RW)  Capture Effect (!802.3) */
#define	TULIP_CMD_BACKPRESSURE	0x00010000L		/* (RW)  Back Pressure (!802.3) (21040) */
#define	TULIP_CMD_THRESHOLDCTL	0x0000C000L		/* (RW)  Threshold Control */
#define	TULIP_CMD_THRSHLD72	0x00000000L		/*       00 - 72 Bytes */
#define	TULIP_CMD_THRSHLD96	0x00004000L		/*       01 - 96 Bytes */
#define	TULIP_CMD_THRSHLD128	0x00008000L		/*       10 - 128 bytes */
#define	TULIP_CMD_THRSHLD160	0x0000C000L		/*       11 - 160 Bytes */
#define	TULIP_CMD_TXRUN 	0x00002000L		/* (RW)  Start/Stop Transmitter */
#define	TULIP_CMD_FORCECOLL	0x00001000L		/* (RW)  Force Collisions */
#define	TULIP_CMD_OPERMODE	0x00000C00L		/* (RW)  Operating Mode */
#define	TULIP_CMD_FULLDUPLEX	0x00000200L		/* (RW)  Full Duplex Mode */
#define	TULIP_CMD_FLAKYOSCDIS	0x00000100L		/* (RW)  Flakey Oscillator Disable */
#define	TULIP_CMD_ALLMULTI	0x00000080L		/* (RW)  Pass All Multicasts */
#define	TULIP_CMD_PROMISCUOUS	0x00000040L		/* (RW)  Promiscuous Mode */
#define	TULIP_CMD_BACKOFFCTR	0x00000020L		/* (RW)  Start/Stop Backoff Counter (!802.3) */
#define	TULIP_CMD_INVFILTER	0x00000010L		/* (R )  Inverse Filtering */
#define	TULIP_CMD_PASSBADPKT	0x00000008L		/* (RW)  Pass Bad Frames  */
#define	TULIP_CMD_HASHONLYFLTR	0x00000004L		/* (R )  Hash Only Filtering */
#define	TULIP_CMD_RXRUN		0x00000002L		/* (RW)  Start/Stop Receive Filtering */
#define	TULIP_CMD_HASHPRFCTFLTR	0x00000001L		/* (R )  Hash/Perfect Receive Filtering */

#define TULIP_SIASTS_OTHERRXACTIVITY	0x00000200L
#define TULIP_SIASTS_RXACTIVITY		0x00000100L
#define	TULIP_SIASTS_LINKFAIL		0x00000004L
#define	TULIP_SIASTS_LINK100FAIL	0x00000002L
#define	TULIP_SIACONN_RESET		0x00000000L

/*
 * 21040 SIA definitions
 */
#define	TULIP_21040_PROBE_10BASET_TIMEOUT	2500
#define	TULIP_21040_PROBE_AUIBNC_TIMEOUT	300
#define	TULIP_21040_PROBE_EXTSIA_TIMEOUT	300

#define	TULIP_21040_SIACONN_10BASET	0x0000EF01L
#define	TULIP_21040_SIATXRX_10BASET	0x0000FFFFL
#define	TULIP_21040_SIAGEN_10BASET	0x00000000L

#define	TULIP_21040_SIACONN_10BASET_FD	0x0000EF01L
#define	TULIP_21040_SIATXRX_10BASET_FD	0x0000FFFDL
#define	TULIP_21040_SIAGEN_10BASET_FD	0x00000000L

#define	TULIP_21040_SIACONN_AUIBNC	0x0000EF09L
#define	TULIP_21040_SIATXRX_AUIBNC	0x00000705L
#define	TULIP_21040_SIAGEN_AUIBNC	0x00000006L

#define	TULIP_21040_SIACONN_EXTSIA	0x00003041L
#define	TULIP_21040_SIATXRX_EXTSIA	0x00000000L
#define	TULIP_21040_SIAGEN_EXTSIA	0x00000006L

/*
 * 21041 SIA definitions
 */

#define	TULIP_21041_PROBE_10BASET_TIMEOUT	2500
#define	TULIP_21041_PROBE_AUIBNC_TIMEOUT	300

#define	TULIP_21041_SIACONN_10BASET		0x0000EF01L
#define	TULIP_21041_SIATXRX_10BASET		0x0000FF3FL
#define	TULIP_21041_SIAGEN_10BASET		0x00000000L

#define	TULIP_21041P2_SIACONN_10BASET		0x0000EF01L
#define	TULIP_21041P2_SIATXRX_10BASET		0x0000FFFFL
#define	TULIP_21041P2_SIAGEN_10BASET		0x00000000L

#define	TULIP_21041_SIACONN_10BASET_FD		0x0000EF01L
#define	TULIP_21041_SIATXRX_10BASET_FD		0x0000FF3DL
#define	TULIP_21041_SIAGEN_10BASET_FD		0x00000000L

#define	TULIP_21041P2_SIACONN_10BASET_FD	0x0000EF01L
#define	TULIP_21041P2_SIATXRX_10BASET_FD	0x0000FFFFL
#define	TULIP_21041P2_SIAGEN_10BASET_FD		0x00000000L

#define	TULIP_21041_SIACONN_AUI			0x0000EF09L
#define	TULIP_21041_SIATXRX_AUI			0x0000F73DL
#define	TULIP_21041_SIAGEN_AUI			0x0000000EL

#define	TULIP_21041P2_SIACONN_AUI		0x0000EF09L
#define	TULIP_21041P2_SIATXRX_AUI		0x0000F7FDL
#define	TULIP_21041P2_SIAGEN_AUI		0x0000000EL

#define	TULIP_21041_SIACONN_BNC			0x0000EF09L
#define	TULIP_21041_SIATXRX_BNC			0x0000F73DL
#define	TULIP_21041_SIAGEN_BNC			0x00000006L

#define	TULIP_21041P2_SIACONN_BNC		0x0000EF09L
#define	TULIP_21041P2_SIATXRX_BNC		0x0000F7FDL
#define	TULIP_21041P2_SIAGEN_BNC		0x00000006L

/*
 * 21142 SIA definitions
 */

#define	TULIP_21142_PROBE_10BASET_TIMEOUT	2500
#define	TULIP_21142_PROBE_AUIBNC_TIMEOUT	300

#define	TULIP_21142_SIACONN_10BASET		0x00000001L
#define	TULIP_21142_SIATXRX_10BASET		0x00007F3FL
#define	TULIP_21142_SIAGEN_10BASET		0x00000008L

#define	TULIP_21142_SIACONN_10BASET_FD		0x00000001L
#define	TULIP_21142_SIATXRX_10BASET_FD		0x00007F3DL
#define	TULIP_21142_SIAGEN_10BASET_FD		0x00000008L

#define	TULIP_21142_SIACONN_AUI			0x00000009L
#define	TULIP_21142_SIATXRX_AUI			0x00000705L
#define	TULIP_21142_SIAGEN_AUI			0x0000000EL

#define	TULIP_21142_SIACONN_BNC			0x00000009L
#define	TULIP_21142_SIATXRX_BNC			0x00000705L
#define	TULIP_21142_SIAGEN_BNC			0x00000006L




#define	TULIP_WATCHDOG_TXDISABLE	0x00000001L
#define	TULIP_WATCHDOG_RXDISABLE	0x00000010L

#define	TULIP_BUSMODE_SWRESET		0x00000001L
#define	TULIP_BUSMODE_DESCSKIPLEN_MASK	0x0000007CL
#define	TULIP_BUSMODE_BIGENDIAN		0x00000080L
#define	TULIP_BUSMODE_BURSTLEN_MASK	0x00003F00L
#define	TULIP_BUSMODE_BURSTLEN_DEFAULT	0x00000000L
#define	TULIP_BUSMODE_BURSTLEN_1LW	0x00000100L
#define	TULIP_BUSMODE_BURSTLEN_2LW	0x00000200L
#define	TULIP_BUSMODE_BURSTLEN_4LW	0x00000400L
#define	TULIP_BUSMODE_BURSTLEN_8LW	0x00000800L
#define	TULIP_BUSMODE_BURSTLEN_16LW	0x00001000L
#define	TULIP_BUSMODE_BURSTLEN_32LW	0x00002000L
#define	TULIP_BUSMODE_CACHE_NOALIGN	0x00000000L
#define	TULIP_BUSMODE_CACHE_ALIGN8	0x00004000L
#define	TULIP_BUSMODE_CACHE_ALIGN16	0x00008000L
#define	TULIP_BUSMODE_CACHE_ALIGN32	0x0000C000L
#define	TULIP_BUSMODE_TXPOLL_NEVER	0x00000000L
#define	TULIP_BUSMODE_TXPOLL_200000ns	0x00020000L
#define	TULIP_BUSMODE_TXPOLL_800000ns	0x00040000L
#define	TULIP_BUSMODE_TXPOLL_1600000ns	0x00060000L
#define	TULIP_BUSMODE_TXPOLL_12800ns	0x00080000L	/* 21041 only */
#define	TULIP_BUSMODE_TXPOLL_25600ns	0x000A0000L	/* 21041 only */
#define	TULIP_BUSMODE_TXPOLL_51200ns	0x000C0000L	/* 21041 only */
#define	TULIP_BUSMODE_TXPOLL_102400ns	0x000E0000L	/* 21041 only */
#define	TULIP_BUSMODE_DESC_BIGENDIAN	0x00100000L	/* 21041 only */
#define	TULIP_BUSMODE_READMULTIPLE	0x00200000L	/* */

#define	TULIP_REG_CFDA			0x40
#define	TULIP_CFDA_SLEEP		0x80000000L
#define	TULIP_CFDA_SNOOZE		0x40000000L

#define	TULIP_GP_PINSET			0x00000100L
/*
 * These are the defintitions used for the DEC 21140
 * evaluation board.
 */
#define	TULIP_GP_EB_PINS		0x0000001F	/* General Purpose Pin directions */
#define	TULIP_GP_EB_OK10		0x00000080	/* 10 Mb/sec Signal Detect gep<7> */
#define	TULIP_GP_EB_OK100		0x00000040	/* 100 Mb/sec Signal Detect gep<6> */
#define	TULIP_GP_EB_INIT		0x0000000B	/* No loopback --- point-to-point */

/*
 * These are the defintitions used for the SMC9332 (21140) board.
 */
#define	TULIP_GP_SMC_9332_PINS		0x0000003F	/* General Purpose Pin directions */
#define	TULIP_GP_SMC_9332_OK10		0x00000080	/* 10 Mb/sec Signal Detect gep<7> */
#define	TULIP_GP_SMC_9332_OK100		0x00000040	/* 100 Mb/sec Signal Detect gep<6> */
#define	TULIP_GP_SMC_9332_INIT		0x00000009	/* No loopback --- point-to-point */

/*
 * There are the definitions used for the DEC DE500
 * 10/100 family of boards
 */
#define	TULIP_GP_DE500_PINS		0x0000001FL
#define	TULIP_GP_DE500_LINK_PASS	0x00000080L
#define	TULIP_GP_DE500_SYM_LINK		0x00000040L
#define	TULIP_GP_DE500_SIGNAL_DETECT	0x00000020L
#define	TULIP_GP_DE500_PHY_RESET	0x00000010L
#define	TULIP_GP_DE500_HALFDUPLEX	0x00000008L
#define	TULIP_GP_DE500_PHY_LOOPBACK	0x00000004L
#define	TULIP_GP_DE500_FORCE_LED	0x00000002L
#define	TULIP_GP_DE500_FORCE_100	0x00000001L

/*
 * These are the defintitions used for the Cogent EM100
 * 21140 board.
 */
#define	TULIP_GP_EM100_PINS		0x0000003F	/* General Purpose Pin directions */
#define	TULIP_GP_EM100_INIT		0x00000009	/* No loopback --- point-to-point */
#define	TULIP_COGENT_EM100TX_ID		0x12
#define	TULIP_COGENT_EM100FX_ID		0x15


/*
 * These are the defintitions used for the Znyx ZX342
 * 10/100 board
 */
#define	TULIP_ZNYX_ID_ZX312		0x0602
#define	TULIP_ZNYX_ID_ZX312T		0x0622
#define	TULIP_ZNYX_ID_ZX314_INTA	0x0701
#define	TULIP_ZNYX_ID_ZX314		0x0711
#define	TULIP_ZNYX_ID_ZX315_INTA	0x0801
#define	TULIP_ZNYX_ID_ZX315		0x0811
#define	TULIP_ZNYX_ID_ZX342		0x0901
#define	TULIP_ZNYX_ID_ZX342B		0x0921
#define	TULIP_ZNYX_ID_ZX342_X3		0x0902
#define	TULIP_ZNYX_ID_ZX342_X4		0x0903
#define	TULIP_ZNYX_ID_ZX344		0x0A01
#define	TULIP_ZNYX_ID_ZX351		0x0B01
#define	TULIP_ZNYX_ID_ZX345		0x0C01
#define	TULIP_ZNYX_ID_ZX311		0x0D01
#define	TULIP_ZNYX_ID_ZX346		0x0E01

#define	TULIP_GP_ZX34X_PINS		0x0000001F	/* General Purpose Pin directions */
#define	TULIP_GP_ZX344_PINS		0x0000000B	/* General Purpose Pin directions */
#define	TULIP_GP_ZX345_PINS		0x00000003	/* General Purpose Pin directions */
#define	TULIP_GP_ZX346_PINS		0x00000043	/* General Purpose Pin directions */
#define	TULIP_GP_ZX34X_LNKFAIL		0x00000080	/* 10Mb/s Link Failure */
#define	TULIP_GP_ZX34X_SYMDET		0x00000040	/* 100Mb/s Symbol Detect */
#define	TULIP_GP_ZX345_PHYACT		0x00000040	/* PHY Activity */
#define	TULIP_GP_ZX34X_SIGDET		0x00000020	/* 100Mb/s Signal Detect */
#define	TULIP_GP_ZX346_AUTONEG_ENABLED	0x00000020	/* 802.3u autoneg enabled */
#define	TULIP_GP_ZX342_COLENA		0x00000008	/* 10t Ext LB */
#define	TULIP_GP_ZX344_ROTINT		0x00000008	/* PPB IRQ rotation */
#define	TULIP_GP_ZX345_SPEED10		0x00000008	/* 10Mb speed detect */
#define	TULIP_GP_ZX346_SPEED100		0x00000008	/* 100Mb speed detect */
#define	TULIP_GP_ZX34X_NCOLENA		0x00000004	/* 10t Int LB */
#define	TULIP_GP_ZX34X_RXMATCH		0x00000004	/* RX Match */
#define	TULIP_GP_ZX346_FULLDUPLEX	0x00000004	/* Full Duplex Sensed */
#define	TULIP_GP_ZX34X_LB102		0x00000002	/* 100tx twister LB */
#define	TULIP_GP_ZX34X_NLB101		0x00000001	/* PDT/PDR LB */
#define	TULIP_GP_ZX34X_INIT		0x00000009	

/*
 * Asante's stuff...
 */
#define TULIP_GP_ASANTE_PINS		0x000000bf	/* GP pin config */
#define TULIP_GP_ASANTE_PHYRESET	0x00000008	/* Reset PHY */

/*
 * ACCTON EN1207 specialties
 */

#define TULIP_CSR8_EN1207		0x08
#define TULIP_CSR9_EN1207		0x00
#define TULIP_CSR10_EN1207		0x03
#define TULIP_CSR11_EN1207		0x1F

#define TULIP_GP_EN1207_BNC_INIT        0x0000011B
#define TULIP_GP_EN1207_UTP_INIT        0x9E00000B
#define TULIP_GP_EN1207_100_INIT        0x6D00031B

/*
 * SROM definitions for the 21140 and 21041.
 */
#define	SROMXREG	0x0400
#define SROMSEL         0x0800
#define SROMRD          0x4000
#define SROMWR          0x2000
#define SROMDIN         0x0008
#define SROMDOUT        0x0004
#define SROMDOUTON      0x0004
#define SROMDOUTOFF     0x0004
#define SROMCLKON       0x0002
#define SROMCLKOFF      0x0002
#define SROMCSON        0x0001
#define SROMCSOFF       0x0001
#define SROMCS          0x0001

#define	SROMCMD_MODE	4
#define	SROMCMD_WR	5
#define	SROMCMD_RD	6

#define	SROM_BITWIDTH	6

/*
 * MII Definitions for the 21041 and 21140/21140A/21142
 */
#define	MII_PREAMBLE		(~0)
#define	MII_TEST		0xAAAAAAAA
#define	MII_RDCMD		0xF6		/* 1111.0110 */
#define	MII_WRCMD		0xF5		/* 1111.0101 */
#define	MII_DIN			0x00080000
#define	MII_RD			0x00040000
#define	MII_WR			0x00000000
#define	MII_DOUT		0x00020000
#define	MII_CLK			0x00010000
#define	MII_CLKON		MII_CLK
#define	MII_CLKOFF		MII_CLK

#define	PHYREG_CONTROL			0
#define	PHYREG_STATUS			1
#define	PHYREG_IDLOW			2
#define	PHYREG_IDHIGH			3
#define	PHYREG_AUTONEG_ADVERTISEMENT	4
#define	PHYREG_AUTONEG_ABILITIES	5
#define	PHYREG_AUTONEG_EXPANSION	6
#define	PHYREG_AUTONEG_NEXTPAGE		7

#define	PHYSTS_100BASET4	0x8000
#define	PHYSTS_100BASETX_FD	0x4000
#define	PHYSTS_100BASETX	0x2000
#define	PHYSTS_10BASET_FD	0x1000
#define	PHYSTS_10BASET		0x0800
#define	PHYSTS_AUTONEG_DONE	0x0020
#define	PHYSTS_REMOTE_FAULT	0x0010
#define	PHYSTS_CAN_AUTONEG	0x0008
#define	PHYSTS_LINK_UP		0x0004
#define	PHYSTS_JABBER_DETECT	0x0002
#define	PHYSTS_EXTENDED_REGS	0x0001

#define	PHYCTL_RESET		0x8000
#define	PHYCTL_SELECT_100MB	0x2000
#define	PHYCTL_AUTONEG_ENABLE	0x1000
#define	PHYCTL_ISOLATE		0x0400
#define	PHYCTL_AUTONEG_RESTART	0x0200
#define	PHYCTL_FULL_DUPLEX	0x0100

#define	DEC_VENDORID		0x1011
#define	CHIPID_21040		0x0002
#define	CHIPID_21140		0x0009
#define	CHIPID_21041		0x0014
#define	CHIPID_21142		0x0019
#define	PCI_VENDORID(x)		((x) & 0xFFFF)
#define	PCI_CHIPID(x)		(((x) >> 16) & 0xFFFF)

/*
 * Generic SROM Format
 *
 *
 */

typedef struct {
    u_int8_t sh_idbuf[18];
    u_int8_t sh_version;
    u_int8_t sh_adapter_count;
    u_int8_t sh_ieee802_address[6];
} tulip_srom_header_t;

typedef struct {
    u_int8_t sai_device;
    u_int8_t sai_leaf_offset_lowbyte;
    u_int8_t sai_leaf_offset_highbyte;
} tulip_srom_adapter_info_t;

typedef enum {
    TULIP_SROM_CONNTYPE_10BASET			=0x0000,
    TULIP_SROM_CONNTYPE_BNC			=0x0001,
    TULIP_SROM_CONNTYPE_AUI			=0x0002,
    TULIP_SROM_CONNTYPE_100BASETX		=0x0003,
    TULIP_SROM_CONNTYPE_100BASET4		=0x0006,
    TULIP_SROM_CONNTYPE_100BASEFX		=0x0007,
    TULIP_SROM_CONNTYPE_MII_10BASET		=0x0009,
    TULIP_SROM_CONNTYPE_MII_100BASETX		=0x000D,
    TULIP_SROM_CONNTYPE_MII_100BASET4		=0x000F,
    TULIP_SROM_CONNTYPE_MII_100BASEFX		=0x0010,
    TULIP_SROM_CONNTYPE_10BASET_NWAY		=0x0100,
    TULIP_SROM_CONNTYPE_10BASET_FD		=0x0204,
    TULIP_SROM_CONNTYPE_MII_10BASET_FD		=0x020A,
    TULIP_SROM_CONNTYPE_100BASETX_FD		=0x020E,
    TULIP_SROM_CONNTYPE_MII_100BASETX_FD	=0x0211,
    TULIP_SROM_CONNTYPE_10BASET_NOLINKPASS	=0x0400,
    TULIP_SROM_CONNTYPE_AUTOSENSE		=0x0800,
    TULIP_SROM_CONNTYPE_AUTOSENSE_POWERUP	=0x8800,
    TULIP_SROM_CONNTYPE_AUTOSENSE_NWAY		=0x9000,
    TULIP_SROM_CONNTYPE_NOT_USED		=0xFFFF
} tulip_srom_connection_t;

typedef enum {
    TULIP_SROM_MEDIA_10BASET			=0x0000,
    TULIP_SROM_MEDIA_BNC			=0x0001,
    TULIP_SROM_MEDIA_AUI			=0x0002,
    TULIP_SROM_MEDIA_100BASETX			=0x0003,
    TULIP_SROM_MEDIA_10BASET_FD			=0x0004,
    TULIP_SROM_MEDIA_100BASETX_FD		=0x0005,
    TULIP_SROM_MEDIA_100BASET4			=0x0006,
    TULIP_SROM_MEDIA_100BASEFX			=0x0007,
    TULIP_SROM_MEDIA_100BASEFX_FD		=0x0008
} tulip_srom_media_t;

#define	TULIP_SROM_21041_EXTENDED	0x40

#define	TULIP_SROM_2114X_NOINDICATOR	0x8000
#define	TULIP_SROM_2114X_DEFAULT	0x4000
#define	TULIP_SROM_2114X_POLARITY	0x0080
#define	TULIP_SROM_2114X_CMDBITS(n)	(((n) & 0x0071) << 18)
#define	TULIP_SROM_2114X_BITPOS(b)	(1 << (((b) & 0x0E) >> 1))



#endif /* !defined(_DC21040_H) */
