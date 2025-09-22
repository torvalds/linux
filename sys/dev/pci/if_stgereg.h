/*	$OpenBSD: if_stgereg.h,v 1.11 2009/08/10 19:41:05 deraadt Exp $	*/
/*	$NetBSD: if_stgereg.h,v 1.3 2003/02/10 21:10:07 christos Exp $	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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

#ifndef _DEV_PCI_IF_STGEREG_H_
#define	_DEV_PCI_IF_STGEREG_H_

/*
 * Register description for the Sundance Tech. TC9021 10/100/1000
 * Ethernet controller.
 *
 * Note that while DMA addresses are all in 64-bit fields, only
 * the lower 40 bits of a DMA address are valid.
 */

/*
 * Register access macros
 */
#define CSR_WRITE_4(_sc, reg, val)	\
	bus_space_write_4((_sc)->sc_st, (_sc)->sc_sh, (reg), (val))
#define CSR_WRITE_2(_sc, reg, val)	\
	bus_space_write_2((_sc)->sc_st, (_sc)->sc_sh, (reg), (val))
#define CSR_WRITE_1(_sc, reg, val)	\
	bus_space_write_1((_sc)->sc_st, (_sc)->sc_sh, (reg), (val))

#define CSR_READ_4(_sc, reg)		\
	bus_space_read_4((_sc)->sc_st, (_sc)->sc_sh, (reg))
#define CSR_READ_2(_sc, reg)		\
	bus_space_read_2((_sc)->sc_st, (_sc)->sc_sh, (reg))
#define CSR_READ_1(_sc, reg)		\
	bus_space_read_1((_sc)->sc_st, (_sc)->sc_sh, (reg))

/*
 * TC9021 buffer fragment descriptor.
 */
struct stge_frag {
	uint64_t	frag_word0;	/* address, length */
} __packed;

#define	FRAG_ADDR(x)	(((uint64_t)(x)) << 0)
#define	FRAG_ADDR_MASK	FRAG_ADDR(0xfffffffffULL)
#define	FRAG_LEN(x)	(((uint64_t)(x)) << 48)
#define	FRAG_LEN_MASK	FRAG_LEN(0xffffULL)

/*
 * TC9021 Transmit Frame Descriptor.  Note the number of fragments
 * here is arbitrary, but we can't have any more than 15.
 */
#define	STGE_NTXFRAGS	12
struct stge_tfd {
	uint64_t	tfd_next;	/* next TFD in list */
	uint64_t	tfd_control;	/* control bits */
					/* the buffer fragments */
	struct stge_frag tfd_frags[STGE_NTXFRAGS];
} __packed;

#define	TFD_FrameId(x)		((x) << 0)
#define	TFD_FrameId_MAX		0xffff
#define	TFD_WordAlign(x)	((x) << 16)
#define	TFD_WordAlign_dword	0		/* align to dword in TxFIFO */
#define	TFD_WordAlign_word	2		/* align to word in TxFIFO */
#define	TFD_WordAlign_disable	1		/* disable alignment */
#define	TFD_TCPChecksumEnable	(1ULL << 18)
#define	TFD_UDPChecksumEnable	(1ULL << 19)
#define	TFD_IPChecksumEnable	(1ULL << 20)
#define	TFD_FcsAppendDisable	(1ULL << 21)
#define	TFD_TxIndicate		(1ULL << 22)
#define	TFD_TxDMAIndicate	(1ULL << 23)
#define	TFD_FragCount(x)	((x) << 24)
#define	TFD_VLANTagInsert	(1ULL << 28)
#define	TFD_TFDDone		(1ULL << 31)
#define	TFD_VID(x)		(((uint64_t)(x)) << 32)
#define	TFD_CFI			(1ULL << 44)
#define	TFD_UserPriority(x)	(((uint64_t)(x)) << 45)

/*
 * TC9021 Receive Frame Descriptor.  Each RFD has a single fragment
 * in it, and the chip tells us the beginning and end of the frame.
 */
struct stge_rfd {
	uint64_t	rfd_next;	/* next RFD in list */
	uint64_t	rfd_status;	/* status bits */
	struct stge_frag rfd_frag;	/* the buffer */
} __packed;

#define	RFD_RxDMAFrameLen(x)	((x) & 0xffff)
#define	RFD_RxFIFOOverrun	(1ULL << 16)
#define	RFD_RxRuntFrame		(1ULL << 17)
#define	RFD_RxAlignmentError	(1ULL << 18)
#define	RFD_RxFCSError		(1ULL << 19)
#define	RFD_RxOversizedFrame	(1ULL << 20)
#define	RFD_RxLengthError	(1ULL << 21)
#define	RFD_VLANDetected	(1ULL << 22)
#define	RFD_TCPDetected		(1ULL << 23)
#define	RFD_TCPError		(1ULL << 24)
#define	RFD_UDPDetected		(1ULL << 25)
#define	RFD_UDPError		(1ULL << 26)
#define	RFD_IPDetected		(1ULL << 27)
#define	RFD_IPError		(1ULL << 28)
#define	RFD_FrameStart		(1ULL << 29)
#define	RFD_FrameEnd		(1ULL << 30)
#define	RFD_RFDDone		(1ULL << 31)
#define	RFD_TCI(x)		((((uint64_t)(x)) >> 32) & 0xffff)

/*
 * PCI configuration registers used by the TC9021.
 */

#define	STGE_PCI_IOBA		(PCI_MAPREG_START + 0x00)
#define	STGE_PCI_MMBA		(PCI_MAPREG_START + 0x04)

/*
 * EEPROM offsets.
 */
#define	STGE_EEPROM_ConfigParam		0x00
#define	STGE_EEPROM_AsicCtrl		0x01
#define	STGE_EEPROM_SubSystemVendorId	0x02
#define	STGE_EEPROM_SubSystemId		0x03
#define	STGE_EEPROM_StationAddress0	0x10
#define	STGE_EEPROM_StationAddress1	0x11
#define	STGE_EEPROM_StationAddress2	0x12

/*
 * The TC9021 register space.
 */

#define	STGE_DMACtrl			0x00
#define	DMAC_RxDMAComplete		(1U << 3)
#define	DMAC_RxDMAPollNow		(1U << 4)
#define	DMAC_TxDMAComplete		(1U << 11)
#define	DMAC_TxDMAPollNow		(1U << 12)
#define	DMAC_TxDMAInProg		(1U << 15)
#define	DMAC_RxEarlyDisable		(1U << 16)
#define	DMAC_MWIDisable			(1U << 18)
#define	DMAC_TxWiteBackDisable		(1U << 19)
#define	DMAC_TxBurstLimit(x)		((x) << 20)
#define	DMAC_TargetAbort		(1U << 30)
#define	DMAC_MasterAbort		(1U << 31)

#define	STGE_RxDMAStatus		0x08

#define	STGE_TFDListPtrLo		0x10

#define	STGE_TFDListPtrHi		0x14

#define	STGE_TxDMABurstThresh		0x18	/* 8-bit */

#define	STGE_TxDMAUrgentThresh		0x19	/* 8-bit */

#define	STGE_TxDMAPollPeriod		0x1a	/* 8-bit */

#define	STGE_RFDListPtrLo		0x1c

#define	STGE_RFDListPtrHi		0x20

#define	STGE_RxDMABurstThresh		0x24	/* 8-bit */

#define	STGE_RxDMAUrgentThresh		0x25	/* 8-bit */

#define	STGE_RxDMAPollPeriod		0x26	/* 8-bit */

#define	STGE_RxDMAIntCtrl		0x28
#define	RDIC_RxFrameCount(x)		((x) & 0xff)
#define	RDIC_PriorityThresh(x)		((x) << 10)
#define	RDIC_RxDMAWaitTime(x)		((x) << 16)

#define	STGE_DebugCtrl			0x2c	/* 16-bit */
#define	DC_GPIO0Ctrl			(1U << 0)
#define	DC_GPIO1Ctrl			(1U << 1)
#define	DC_GPIO0			(1U << 2)
#define	DC_GPIO1			(1U << 3)

#define	STGE_AsicCtrl			0x30
#define	AC_ExpRomDisable		(1U << 0)
#define	AC_ExpRomSize			(1U << 1)
#define	AC_PhySpeed10			(1U << 4)
#define	AC_PhySpeed100			(1U << 5)
#define	AC_PhySpeed1000			(1U << 6)
#define	AC_PhyMedia			(1U << 7)
#define	AC_ForcedConfig(x)		((x) << 8)
#define	AC_ForcedConfig_MASK		AC_ForcedConfig(7)
#define	AC_D3ResetDisable		(1U << 11)
#define	AC_SpeedupMode			(1U << 13)
#define	AC_LEDMode			(1U << 14)
#define	AC_RstOutPolarity		(1U << 15)
#define	AC_GlobalReset			(1U << 16)
#define	AC_RxReset			(1U << 17)
#define	AC_TxReset			(1U << 18)
#define	AC_DMA				(1U << 19)
#define	AC_FIFO				(1U << 20)
#define	AC_Network			(1U << 21)
#define	AC_Host				(1U << 22)
#define	AC_AutoInit			(1U << 23)
#define	AC_RstOut			(1U << 24)
#define	AC_InterruptRequest		(1U << 25)
#define	AC_ResetBusy			(1U << 26)

#define	STGE_FIFOCtrl			0x38	/* 16-bit */
#define	FC_RAMTestMode			(1U << 0)
#define	FC_Transmitting			(1U << 14)
#define	FC_Receiving			(1U << 15)

#define	STGE_RxEarlyThresh		0x3a	/* 16-bit */

#define	STGE_FlowOffThresh		0x3c	/* 16-bit */

#define	STGE_FlowOnTresh		0x3e	/* 16-bit */

#define	STGE_TxStartThresh		0x44	/* 16-bit */

#define	STGE_EepromData			0x48	/* 16-bit */

#define	STGE_EepromCtrl			0x4a	/* 16-bit */
#define	EC_EepromAddress(x)		((x) & 0xff)
#define	EC_EepromOpcode(x)		((x) << 8)
#define	EC_OP_WE			0
#define	EC_OP_WR			1
#define	EC_OP_RR			2
#define	EC_OP_ER			3
#define	EC_EepromBusy			(1U << 15)

#define	STGE_ExpRomAddr			0x4c

#define	STGE_ExpRomData			0x50	/* 8-bit */

#define	STGE_WakeEvent			0x51	/* 8-bit */

#define	STGE_Countdown			0x54
#define	CD_Count(x)			((x) & 0xffff)
#define	CD_CountdownSpeed		(1U << 24)
#define	CD_CountdownMode		(1U << 25)
#define	CD_CountdownIntEnabled		(1U << 26)

#define	STGE_IntStatusAck		0x5a	/* 16-bit */

#define	STGE_IntStatus			0x5e	/* 16-bit */

#define	STGE_IntEnable			0x5c	/* 16-bit */

#define	IS_InterruptStatus		(1U << 0)
#define	IS_HostError			(1U << 1)
#define	IS_TxComplete			(1U << 2)
#define	IS_MACControlFrame		(1U << 3)
#define	IS_RxComplete			(1U << 4)
#define	IS_RxEarly			(1U << 5)
#define	IS_InRequested			(1U << 6)
#define	IS_UpdateStats			(1U << 7)
#define	IS_LinkEvent			(1U << 8)
#define	IS_TxDMAComplete		(1U << 9)
#define	IS_RxDMAComplete		(1U << 10)
#define	IS_RFDListEnd			(1U << 11)
#define	IS_RxDMAPriority		(1U << 12)

#define	STGE_TxStatus			0x60
#define	TS_TxError			(1U << 0)
#define	TS_LateCollision		(1U << 2)
#define	TS_MaxCollisions		(1U << 3)
#define	TS_TxUnderrun			(1U << 4)
#define	TS_TxIndicateReqd		(1U << 6)
#define	TS_TxComplete			(1U << 7)
#define	TS_TxFrameId_get(x)		((x) >> 16)

#define	STGE_MACCtrl			0x6c
#define	MC_IFSSelect(x)			((x) & 3)
#define	MC_DuplexSelect			(1U << 5)
#define	MC_RcvLargeFrames		(1U << 6)
#define	MC_TxFlowControlEnable		(1U << 7)
#define	MC_RxFlowControlEnable		(1U << 8)
#define	MC_RcvFCS			(1U << 9)
#define	MC_FIFOLoopback			(1U << 10)
#define	MC_MACLoopback			(1U << 11)
#define	MC_AutoVLANtagging		(1U << 12)
#define	MC_AutoVLANuntagging		(1U << 13)
#define	MC_CollisionDetect		(1U << 16)
#define	MC_CarrierSense			(1U << 17)
#define	MC_StatisticsEnable		(1U << 21)
#define	MC_StatisticsDisable		(1U << 22)
#define	MC_StatisticsEnabled		(1U << 23)
#define	MC_TxEnable			(1U << 24)
#define	MC_TxDisable			(1U << 25)
#define	MC_TxEnabled			(1U << 26)
#define	MC_RxEnable			(1U << 27)
#define	MC_RxDisable			(1U << 28)
#define	MC_RxEnabled			(1U << 29)
#define	MC_Paused			(1U << 30)

#define	STGE_VLANTag			0x70

#define	STGE_PhyCtrl			0x76	/* 8-bit */
#define	PC_MgmtClk			(1U << 0)
#define	PC_MgmtData			(1U << 1)
#define	PC_MgmtDir			(1U << 2)	/* MAC->PHY */
#define	PC_PhyDuplexPolarity		(1U << 3)
#define	PC_PhyDuplexStatus		(1U << 4)
#define	PC_PhyLnkPolarity		(1U << 5)
#define	PC_LinkSpeed(x)			(((x) >> 6) & 3)
#define	PC_LinkSpeed_Down		0
#define	PC_LinkSpeed_10			1
#define	PC_LinkSpeed_100		2
#define	PC_LinkSpeed_1000		3

#define	STGE_StationAddress0		0x78	/* 16-bit */

#define	STGE_StationAddress1		0x7a	/* 16-bit */

#define	STGE_StationAddress2		0x7c	/* 16-bit */

#define	STGE_VLANHashTable		0x7e	/* 16-bit */

#define	STGE_VLANId			0x80

#define	STGE_MaxFrameSize		0x86

#define	STGE_ReceiveMode		0x88	/* 16-bit */
#define	RM_ReceiveUnicast		(1U << 0)
#define	RM_ReceiveMulticast		(1U << 1)
#define	RM_ReceiveBroadcast		(1U << 2)
#define	RM_ReceiveAllFrames		(1U << 3)
#define	RM_ReceiveMulticastHash		(1U << 4)
#define	RM_ReceiveIPMulticast		(1U << 5)
#define	RM_ReceiveVLANMatch		(1U << 8)
#define	RM_ReceiveVLANHash		(1U << 9)

#define	STGE_HashTable0			0x8c

#define	STGE_HashTable1			0x90

#define	STGE_RMONStatisticsMask		0x98	/* set to disable */

#define	STGE_StatisticsMask		0x9c	/* set to disable */

#define	STGE_RxJumboFrames		0xbc	/* 16-bit */

#define	STGE_TCPCheckSumErrors		0xc0	/* 16-bit */

#define	STGE_IPCheckSumErrors		0xc2	/* 16-bit */

#define	STGE_UDPCheckSumErrors		0xc4	/* 16-bit */

#define	STGE_TxJumboFrames		0xf4	/* 16-bit */

/*
 * TC9021 statistics.  Available memory and I/O mapped.
 */

#define	STGE_OctetRcvOk			0xa8

#define	STGE_McstOctetRcvdOk		0xac

#define	STGE_BcstOctetRcvdOk		0xb0

#define	STGE_FramesRcvdOk		0xb4

#define	STGE_McstFramesRcvdOk		0xb8

#define	STGE_BcstFramesRcvdOk		0xbe	/* 16-bit */

#define	STGE_MacControlFramesRcvd	0xc6	/* 16-bit */

#define	STGE_FrameTooLongErrors		0xc8	/* 16-bit */

#define	STGE_InRangeLengthErrors	0xca	/* 16-bit */

#define	STGE_FramesCheckSeqErrors	0xcc	/* 16-bit */

#define	STGE_FramesLostRxErrors		0xce	/* 16-bit */

#define	STGE_OctetXmtdOk		0xd0

#define	STGE_McstOctetXmtdOk		0xd4

#define	STGE_BcstOctetXmtdOk		0xd8

#define	STGE_FramesXmtdOk		0xdc

#define	STGE_McstFramesXmtdOk		0xe0

#define	STGE_FramesWDeferredXmt		0xe4

#define	STGE_LateCollisions		0xe8

#define	STGE_MultiColFrames		0xec

#define	STGE_SingleColFrames		0xf0

#define	STGE_BcstFramesXmtdOk		0xf6	/* 16-bit */

#define	STGE_CarrierSenseErrors		0xf8	/* 16-bit */

#define	STGE_MacControlFramesXmtd	0xfa	/* 16-bit */

#define	STGE_FramesAbortXSColls		0xfc	/* 16-bit */

#define	STGE_FramesWEXDeferal		0xfe	/* 16-bit */

/*
 * RMON-compatible statistics.  Only accessible if memory-mapped.
 */

#define	STGE_EtherStatsCollisions			0x100

#define	STGE_EtherStatsOctetsTransmit			0x104

#define	STGE_EtherStatsPktsTransmit			0x108

#define	STGE_EtherStatsPkts64OctetsTransmit		0x10c

#define	STGE_EtherStatsPkts64to127OctetsTransmit	0x110

#define	STGE_EtherStatsPkts128to255OctetsTransmit	0x114

#define	STGE_EtherStatsPkts256to511OctetsTransmit	0x118

#define	STGE_EtherStatsPkts512to1023OctetsTransmit	0x11c

#define	STGE_EtherStatsPkts1024to1518OctetsTransmit	0x120

#define	STGE_EtherStatsCRCAlignErrors			0x124

#define	STGE_EtherStatsUndersizePkts			0x128

#define	STGE_EtherStatsFragments			0x12c

#define	STGE_EtherStatsJabbers				0x130

#define	STGE_EtherStatsOctets				0x134

#define	STGE_EtherStatsPkts				0x138

#define	STGE_EtherStatsPkts64Octets			0x13c

#define	STGE_EtherStatsPkts65to127Octets		0x140

#define	STGE_EtherStatsPkts128to255Octets		0x144

#define	STGE_EtherStatsPkts256to511Octets		0x148

#define	STGE_EtherStatsPkts512to1023Octets		0x14c

#define	STGE_EtherStatsPkts1024to1518Octets		0x150

/*
 * Transmit descriptor list size.
 */
#define STGE_NTXDESC		256
#define STGE_NTXDESC_MASK	(STGE_NTXDESC - 1)
#define STGE_NEXTTX(x)		(((x) + 1) & STGE_NTXDESC_MASK)
 
/*
 * Receive descriptor list size.
 */
#define STGE_NRXDESC		256
#define STGE_NRXDESC_MASK	(STGE_NRXDESC - 1)
#define STGE_NEXTRX(x)		(((x) + 1) & STGE_NRXDESC_MASK)

/*
 * Only interrupt every N frames.  Must be a power-of-two.
 */   
#define STGE_TXINTR_SPACING	16
#define STGE_TXINTR_SPACING_MASK (STGE_TXINTR_SPACING - 1)

#define STGE_JUMBO_FRAMELEN	9022
#define STGE_JUMBO_MTU \
	(STGE_JUMBO_FRAMELEN - ETHER_HDR_LEN - ETHER_CRC_LEN)

/*
 * Control structures are DMA'd to the TC9021 chip.  We allocate them in
 * a single clump that maps to a single DMA segment to make several things
 * easier.
 */
struct stge_control_data {
	/*
	 * The transmit descriptors.
	 */
	struct stge_tfd scd_txdescs[STGE_NTXDESC];

	/*
	 * The receive descriptors.
	 */
	struct stge_rfd scd_rxdescs[STGE_NRXDESC];
};

#define STGE_CDOFF(x)	offsetof(struct stge_control_data, x)
#define STGE_CDTXOFF(x)	STGE_CDOFF(scd_txdescs[(x)])
#define STGE_CDRXOFF(x)	STGE_CDOFF(scd_rxdescs[(x)])

/*
 * Software state for transmit and receive jobs.
 */
struct stge_descsoft {
	struct mbuf *ds_mbuf;		/* head of our mbuf chain */
	bus_dmamap_t ds_dmamap;		/* our DMA map */
};

/*
 * Software state per device.
 */
struct stge_softc {
	struct device sc_dev;		/* generic device information */
	bus_space_tag_t sc_st;		/* bus space tag */
	bus_space_handle_t sc_sh;	/* bus space handle */ 
	bus_dma_tag_t sc_dmat;		/* bus DMA tag */ 
	struct arpcom sc_arpcom;	/* ethernet common data */
	int sc_rev;			/* silicon revision */ 
	void *sc_ih;			/* interrupt cookie */

	struct mii_data sc_mii;		/* MII/media information */

	struct timeout sc_timeout;	/* tick timeout */
        
	bus_dmamap_t sc_cddmamap;	/* control data DMA map */
#define sc_cddma	sc_cddmamap->dm_segs[0].ds_addr

	/*
	 * Software state for transmit and receive descriptors.
	*/
	struct stge_descsoft sc_txsoft[STGE_NTXDESC];
	struct stge_descsoft sc_rxsoft[STGE_NRXDESC];

	/*
	 * Control data structures.
	*/
	struct stge_control_data *sc_control_data;
#define sc_txdescs	sc_control_data->scd_txdescs
#define sc_rxdescs	sc_control_data->scd_rxdescs

	int	sc_txpending;		/* number of Tx requests pending */
	int	sc_txdirty;		/* first dirty Tx descriptor */
	int	sc_txlast;		/* last used Tx descriptor */

	int	sc_rxptr;		/* next ready Rx descriptor/descsoft */
	int	sc_rxdiscard;
	int	sc_rxlen;
	struct mbuf *sc_rxhead;
	struct mbuf *sc_rxtail;
	struct mbuf **sc_rxtailp;

	int	sc_txthresh;		/* Tx threshold */
	uint32_t sc_usefiber:1;		/* if we're fiber */
	uint32_t sc_stge1023:1;		/* are we a 1023 */
	uint32_t sc_DMACtrl;		/* prototype DMACtrl register */
	uint32_t sc_MACCtrl;		/* prototype MacCtrl register */
	uint16_t sc_IntEnable;		/* prototype IntEnable register */
	uint16_t sc_ReceiveMode;	/* prototype ReceiveMode register */
	uint8_t sc_PhyCtrl;		/* prototype PhyCtrl register */
};

#define STGE_RXCHAIN_RESET(sc)						\
do {									\
	(sc)->sc_rxtailp = &(sc)->sc_rxhead;				\
	*(sc)->sc_rxtailp = NULL;					\
	(sc)->sc_rxlen = 0;						\
} while (/*CONSTCOND*/0)
 
#define STGE_RXCHAIN_LINK(sc, m)					\
do {									\
	*(sc)->sc_rxtailp = (sc)->sc_rxtail = (m);			\
	(sc)->sc_rxtailp = &(m)->m_next;				\
} while (/*CONSTCOND*/0)

#define STGE_CDTXADDR(sc, x)	((sc)->sc_cddma + STGE_CDTXOFF((x)))
#define STGE_CDRXADDR(sc, x)	((sc)->sc_cddma + STGE_CDRXOFF((x)))

#define STGE_CDTXSYNC(sc, x, ops)					\
	bus_dmamap_sync((sc)->sc_dmat, (sc)->sc_cddmamap,		\
	    STGE_CDTXOFF((x)), sizeof(struct stge_tfd), (ops))

#define STGE_CDRXSYNC(sc, x, ops)					\
	bus_dmamap_sync((sc)->sc_dmat, (sc)->sc_cddmamap,		\
	    STGE_CDRXOFF((x)), sizeof(struct stge_rfd), (ops))

#define STGE_INIT_RXDESC(sc, x)						\
do {									\
	struct stge_descsoft *__ds = &(sc)->sc_rxsoft[(x)];		\
	struct stge_rfd *__rfd = &(sc)->sc_rxdescs[(x)];		\
									\
	/*								\
	 * Note: We scoot the packet forward 2 bytes in the buffer	\
	 * so that the payload after the Ethernet header is aligned	\
	 * to a 4-byte boundary.					\
	 */								\
	__rfd->rfd_frag.frag_word0 =					\
	    htole64(FRAG_ADDR(__ds->ds_dmamap->dm_segs[0].ds_addr + 2) |\
	    FRAG_LEN(MCLBYTES - 2));					\
	__rfd->rfd_next =						\
	    htole64((uint64_t)STGE_CDRXADDR((sc), STGE_NEXTRX((x))));	\
	__rfd->rfd_status = 0;						\
	STGE_CDRXSYNC((sc), (x), BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE); \
} while (/*CONSTCOND*/0)

#define STGE_TIMEOUT	1000

#endif /* _DEV_PCI_IF_STGEREG_H_ */
