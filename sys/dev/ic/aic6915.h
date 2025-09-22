/*	$OpenBSD: aic6915.h,v 1.5 2022/01/09 05:42:38 jsg Exp $	*/
/*	$NetBSD: aic6915reg.h,v 1.4 2005/12/11 12:21:25 christos Exp $	*/

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

#ifndef _DEV_IC_AIC6915_H_
#define	_DEV_IC_AIC6915_H_

#include <sys/timeout.h>

/*
 * Register description for the Adaptec AIC-6915 (``Starfire'')
 * 10/100 Ethernet controller.
 */

/*
 * Receive Buffer Descriptor (One-size, 32-bit addressing)
 */
struct sf_rbd32 {
	uint32_t	rbd32_addr;		/* address, flags */
};

/*
 * Receive Buffer Descriptor (One-size, 64-bit addressing)
 */
struct sf_rbd64 {
	uint32_t	rbd64_addr_lo;		/* address (LSD), flags */
	uint32_t	rbd64_addr_hi;		/* address (MDS) */
};

#define	RBD_V		(1U << 0)	/* valid descriptor */
#define	RBD_E		(1U << 1)	/* end of ring */

/*
 * Short (Type 0) Completion Descriptor
 */
struct sf_rcd_short {
	uint32_t	rcd_word0;	/* length, end index, status1 */
};

/*
 * Basic (Type 1) Completion Descriptor
 */
struct sf_rcd_basic {
	uint32_t	rcd_word0;	/* length, end index, status1 */
	uint32_t	rcd_word1;	/* VLAN ID, status2 */
};

/*
 * Checksum (Type 2) Completion Descriptor
 */
struct sf_rcd_checksum {
	uint32_t	rcd_word0;	/* length, end index, status1 */
	uint32_t	rcd_word1;	/* partial TCP/UDP checksum, status2 */
};

/*
 * Full (Type 3) Completion Descriptor
 */
struct sf_rcd_full {
	uint32_t	rcd_word0;	/* length, end index, status1 */
	uint32_t	rcd_word1;	/* start index, status3, status2 */
	uint32_t	rcd_word2;	/* VLAN ID + priority, TCP/UDP csum */
	uint32_t	rcd_timestamp;	/* timestamp */
};

#define	RCD_W0_ID		(1U << 30)

#define	RCD_W0_Length(x)	((x) & 0xffff)
#define	RCD_W0_EndIndex(x)	(((x) >> 16) & 0x7ff)
#define	RCD_W0_BufferQueue	(1U << 27)	/* 1 == Queue 2 */
#define	RCD_W0_FifoFull		(1U << 28)	/* FIFO full */
#define	RCD_W0_OK		(1U << 29)	/* packet is OK */

/* Status2 field */
#define	RCD_W1_FrameType	(7U << 16)
#define	RCD_W1_FrameType_Unknown (0 << 16)
#define	RCD_W1_FrameType_IPv4	(1U << 16)
#define	RCD_W1_FrameType_IPv6	(2U << 16)
#define	RCD_W1_FrameType_IPX	(3U << 16)
#define	RCD_W1_FrameType_ICMP	(4U << 16)
#define	RCD_W1_FrameType_Unsupported (5U << 16)
#define	RCD_W1_UdpFrame		(1U << 19)
#define	RCD_W1_TcpFrame		(1U << 20)
#define	RCD_W1_Fragmented	(1U << 21)
#define	RCD_W1_PartialChecksumValid (1U << 22)
#define	RCD_W1_ChecksumBad	(1U << 23)
#define	RCD_W1_ChecksumOk	(1U << 24)
#define	RCD_W1_VlanFrame	(1U << 25)
#define	RCD_W1_ReceiveCodeViolation (1U << 26)
#define	RCD_W1_Dribble		(1U << 27)
#define	RCD_W1_ISLCRCerror	(1U << 28)
#define	RCD_W1_CRCerror		(1U << 29)
#define	RCD_W1_Hash		(1U << 30)
#define	RCD_W1_Perfect		(1U << 31)

#define	RCD_W1_VLANID(x)	((x) & 0xffff)
#define	RCD_W1_TCP_UDP_Checksum(x) ((x) & 0xffff)

/* Status3 field */
#define	RCD_W1_Trailer		(1U << 11)
#define	RCD_W1_Header		(1U << 12)
#define	RCD_W1_ControlFrame	(1U << 13)
#define	RCD_W1_PauseFrame	(1U << 14)
#define	RCD_W1_IslFrame		(1U << 15)

#define	RCD_W1_StartIndex(x)	((x) & 0x7ff)

#define	RCD_W2_TCP_UDP_Checksum(x) ((x) >> 16)
#define	RCD_W2_VLANID(x)	((x) & 0xffff)

/*
 * Number of transmit buffer fragments we use.  This is arbitrary, but
 * we choose it carefully; see blow.
 */
#define	SF_NTXFRAGS		15

/*
 * Type 0, 32-bit addressing mode (Frame Descriptor) Transmit Descriptor
 *
 * NOTE: The total length of this structure is: 8 + (15 * 8) == 128
 * This means 16 Tx indices per Type 0 descriptor.  This is important later
 * on; see below.
 */
struct sf_txdesc0 {
	/* skip field */
	uint32_t	td_word0;	/* ID, flags */
	uint32_t	td_word1;	/* Tx buffer count */
	struct {
		uint32_t fr_addr;	/* address */
		uint32_t fr_len;	/* length */
	} td_frags[SF_NTXFRAGS];
};

#define	TD_W1_NTXBUFS		(0xff << 0)

/*
 * Type 1, 32-bit addressing mode (Buffer Descriptor) Transmit Descriptor
 */
struct sf_txdesc1 {
	/* skip field */
	uint32_t	td_word0;	/* ID, flags */
	uint32_t	td_addr;	/* buffer address */
};

#define	TD_W0_ID		(0xb << 28)
#define	TD_W0_INTR		(1U << 27)
#define	TD_W0_END		(1U << 26)
#define	TD_W0_CALTCP		(1U << 25)
#define	TD_W0_CRCEN		(1U << 24)
#define	TD_W0_LEN		(0xffff << 0)
#define	TD_W0_NTXBUFS		(0xff << 16)
#define	TD_W0_NTXBUFS_SHIFT	16

/*
 * Type 2, 64-bit addressing mode (Buffer Descriptor) Transmit Descriptor
 */
struct sf_txdesc2 {
	/* skip field */
	uint32_t	td_word0;	/* ID, flags */
	uint32_t	td_reserved;
	uint32_t	td_addr_lo;	/* buffer address (LSD) */
	uint32_t	td_addr_hi;	/* buffer address (MSD) */
};

/*
 * Transmit Completion Descriptor.
 */
struct sf_tcd {
	uint32_t	tcd_word0;	/* index, priority, flags */
};

#define	TCD_DMA_ID		(0x4 << 29)
#define	TCD_INDEX(x)		((x) & 0x7fff)
#define	TCD_PR			(1U << 15)
#define	TCD_TIMESTAMP(x)	(((x) >> 16) & 0x1fff)

#define	TCD_TX_ID		(0x5 << 29)
#define	TCD_CRCerror		(1U << 16)
#define	TCD_FieldLengthCkError	(1U << 17)
#define	TCD_FieldLengthRngError	(1U << 18)
#define	TCD_PacketTxOk		(1U << 19)
#define	TCD_Deferred		(1U << 20)
#define	TCD_ExDeferral		(1U << 21)
#define	TCD_ExCollisions	(1U << 22)
#define	TCD_LateCollision	(1U << 23)
#define	TCD_LongFrame		(1U << 24)
#define	TCD_FIFOUnderrun	(1U << 25)
#define	TCD_ControlTx		(1U << 26)
#define	TCD_PauseTx		(1U << 27)
#define	TCD_TxPaused		(1U << 28)

/*
 * The Tx indices are in units of 8 bytes, and since we are using
 * Tx descriptors that are 128 bytes long, we need to divide by 16
 * to get the actual index that we care about.
 */
#define	SF_TXDINDEX_TO_HOST(x)		((x) >> 4)
#define	SF_TXDINDEX_TO_CHIP(x)		((x) << 4)

/*
 * To make matters worse, the manual lies about the indices in the
 * completion queue entries.  It claims they are in 8-byte units,
 * but they're actually *BYTES*, which means we need to divide by
 * 128 to get the actual index.
 */
#define	SF_TCD_INDEX_TO_HOST(x)		((x) >> 7)

/*
 * PCI configuration space addresses.
 */
#define	SF_PCI_MEMBA		(PCI_MAPREG_START + 0x00)
#define	SF_PCI_IOBA		(PCI_MAPREG_START + 0x08)

#define	SF_GENREG_OFFSET	0x50000
#define	SF_FUNCREG_SIZE		0x100

/*
 * PCI functional registers.
 */
#define	SF_PciDeviceConfig	0x40
#define	PDC_EnDpeInt		(1U << 31)	/* enable DPE PCIint */
#define	PDC_EnSseInt		(1U << 30)	/* enable SSE PCIint */
#define	PDC_EnRmaInt		(1U << 29)	/* enable RMA PCIint */
#define	PDC_EnRtaInt		(1U << 28)	/* enable RTA PCIint */
#define	PDC_EnStaInt		(1U << 27)	/* enable STA PCIint */
#define	PDC_EnDprInt		(1U << 24)	/* enable DPR PCIint */
#define	PDC_IntEnable		(1U << 23)	/* enable PCI_INTA_ */
#define	PDC_ExternalRegCsWidth	(7U << 20)	/* external chip-sel width */
#define	PDC_StopMWrOnCacheLineDis (1U << 19)
#define	PDC_EpromCsWidth	(7U << 16)
#define	PDC_EnBeLogic		(1U << 15)
#define	PDC_LatencyStopOnCacheLine (1U << 14)
#define	PDC_PCIMstDmaEn		(1U << 13)
#define	PDC_StopOnCachelineEn	(1U << 12)
#define	PDC_FifoThreshold	(0xf << 8)
#define	PDC_FifoThreshold_SHIFT	8
#define	PDC_MemRdCmdEn		(1U << 7)
#define	PDC_StopOnPerr		(1U << 6)
#define	PDC_AbortOnAddrParityErr (1U << 5)
#define	PDC_EnIncrement		(1U << 4)
#define	PDC_System64		(1U << 2)
#define	PDC_Force64		(1U << 1)
#define	PDC_SoftReset		(1U << 0)

#define	SF_BacControl		0x44
#define	BC_DescSwapMode		(0x3 << 6)
#define	BC_DataSwapMode		(0x3 << 4)
#define	BC_SingleDmaMode	(1U << 3)
#define	BC_PreferTxDmaReq	(1U << 2)
#define	BC_PreferRxDmaReq	(1U << 1)
#define	BC_BacDmaEn		(1U << 0)

#define	SF_PciMonitor1		0x48

#define	SF_PciMonitor2		0x4c

#define	SF_PMC			0x50

#define	SF_PMCSR		0x54

#define	SF_PMEvent		0x58

#define	SF_SerialEpromControl	0x60
#define	SEC_InitDone		(1U << 3)
#define	SEC_Idle		(1U << 2)
#define	SEC_WriteEnable		(1U << 1)
#define	SEC_WriteDisable	(1U << 0)

#define	SF_PciComplianceTesting	0x64

#define	SF_IndirectIoAccess	0x68

#define	SF_IndirectIoDataPort	0x6c

/*
 * Ethernet functional registers.
 */
#define	SF_GeneralEthernetCtrl	0x70
#define	GEC_SetSoftInt		(1U << 8)
#define	GEC_TxGfpEn		(1U << 5)
#define	GEC_RxGfpEn		(1U << 4)
#define	GEC_TxDmaEn		(1U << 3)
#define	GEC_RxDmaEn		(1U << 2)
#define	GEC_TransmitEn		(1U << 1)
#define	GEC_ReceiveEn		(1U << 0)

#define	SF_TimersControl	0x74
#define	TC_EarlyRxQ1IntDelayDisable	(1U << 31)
#define	TC_RxQ1DoneIntDelayDisable	(1U << 30)
#define	TC_EarlyRxQ2IntDelayDisable	(1U << 29)
#define	TC_RxQ2DoneIntDelayDisable	(1U << 28)
#define	TC_TimeStampResolution		(1U << 26)
#define	TC_GeneralTimerResolution	(1U << 25)
#define	TC_OneShotMode			(1U << 24)
#define	TC_GeneralTimerInterval		(0xff << 16)
#define	TC_GeneralTimerInterval_SHIFT	16
#define	TC_TxFrameCompleteIntDelayDisable (1U << 15)
#define	TC_TxQueueDoneIntDelayDisable	(1U << 14)
#define	TC_TxDmaDoneIntDelayDisable	(1U << 13)
#define	TC_RxHiPrBypass			(1U << 12)
#define	TC_Timer10X			(1U << 11)
#define	TC_SmallRxFrame			(3U << 9)
#define	TC_SmallFrameBypass		(1U << 8)
#define	TC_IntMaskMode			(3U << 5)
#define	TC_IntMaskPeriod		(0x1f << 0)

#define	SF_CurrentTime		0x78

#define	SF_InterruptStatus	0x80
#define	IS_GPIO3			(1U << 31)
#define	IS_GPIO2			(1U << 30)
#define	IS_GPIO1			(1U << 29)
#define	IS_GPIO0			(1U << 28)
#define	IS_StatisticWrapInt		(1U << 27)
#define	IS_AbnormalInterrupt		(1U << 25)
#define	IS_GeneralTimerInt		(1U << 24)
#define	IS_SoftInt			(1U << 23)
#define	IS_RxCompletionQueue1Int	(1U << 22)
#define	IS_TxCompletionQueueInt		(1U << 21)
#define	IS_PCIInt			(1U << 20)
#define	IS_DmaErrInt			(1U << 19)
#define	IS_TxDataLowInt			(1U << 18)
#define	IS_RxCompletionQueue2Int	(1U << 17)
#define	IS_RxQ1LowBuffersInt		(1U << 16)
#define	IS_NormalInterrupt		(1U << 15)
#define	IS_TxFrameCompleteInt		(1U << 14)
#define	IS_TxDmaDoneInt			(1U << 13)
#define	IS_TxQueueDoneInt		(1U << 12)
#define	IS_EarlyRxQ2Int			(1U << 11)
#define	IS_EarlyRxQ1Int			(1U << 10)
#define	IS_RxQ2DoneInt			(1U << 9)
#define	IS_RxQ1DoneInt			(1U << 8)
#define	IS_RxGfpNoResponseInt		(1U << 7)
#define	IS_RxQ2LowBuffersInt		(1U << 6)
#define	IS_NoTxChecksumInt		(1U << 5)
#define	IS_TxLowPrMismatchInt		(1U << 4)
#define	IS_TxHiPrMismatchInt		(1U << 3)
#define	IS_GfpRxInt			(1U << 2)
#define	IS_GfpTxInt			(1U << 1)
#define	IS_PCIPadInt			(1U << 0)

#define	SF_ShadowInterruptStatus 0x84

#define	SF_InterruptEn		0x88

#define	SF_GPIO			0x8c
#define	GPIOCtrl(x)		(1U << (24 + (x)))
#define	GPIOOutMode(x)		(1U << (16 + (x)))
#define	GPIOInpMode(x, y)	((y) << (8 + ((x) * 2)))
#define	GPIOData(x)		(1U << (x))

#define	SF_TxDescQueueCtrl	0x90
#define	TDQC_TxHighPriorityFifoThreshold(x)	((x) << 24)
#define	TDQC_SkipLength(x)			((x) << 16)
#define	TDQC_TxDmaBurstSize(x)			((x) << 8)
#define	TDQC_TxDescQueue64bitAddr		(1U << 7)
#define	TDQC_MinFrameSpacing(x)			((x) << 4)
#define	TDQC_DisableTxDmaCompletion		(1U << 3)
#define	TDQC_TxDescType(x)			((x) << 0)

#define	SF_HiPrTxDescQueueBaseAddr 0x94

#define	SF_LoPrTxDescQueueBaseAddr 0x98

#define	SF_TxDescQueueHighAddr	0x9c

#define	SF_TxDescQueueProducerIndex 0xa0
#define	TDQPI_HiPrTxProducerIndex(x)		((x) << 16)
#define	TDQPI_LoPrTxProducerIndex(x)		((x) << 0)
#define	TDQPI_HiPrTxProducerIndex_get(x)	(((x) >> 16) & 0x7ff)
#define	TDQPI_LoPrTxProducerIndex_get(x)	(((x) >> 0) & 0x7ff)

#define	SF_TxDescQueueConsumerIndex 0xa4
#define	TDQCI_HiPrTxConsumerIndex(x)		(((x) >> 16) & 0x7ff)
#define	TDQCI_LoPrTxConsumerIndex(s)		(((x) >> 0) & 0x7ff)

#define	SF_TxDmaStatus1		0xa8

#define	SF_TxDmaStatus2		0xac

#define	SF_TransmitFrameCSR	0xb0
#define	TFCSR_TxFrameStatus			(0xff << 16)
#define	TFCSR_TxDebugConfigBits			(0x7f << 9)
#define	TFCSR_DmaCompletionAfterTransmitComplete (1U << 8)
#define	TFCSR_TransmitThreshold(x)		((x) << 0)

#define	SF_CompletionQueueHighAddr 0xb4

#define	SF_TxCompletionQueueCtrl 0xb8
#define	TCQC_TxCompletionBaseAddress		0xffffff00
#define	TCQC_TxCompletion64bitAddress		(1U << 7)
#define	TCQC_TxCompletionProducerWe		(1U << 6)
#define	TCQC_TxCompletionSize			(1U << 5)
#define	TCQC_CommonQueueMode			(1U << 4)
#define	TCQC_TxCompletionQueueThreshold		((x) << 0)

#define	SF_RxCompletionQueue1Ctrl 0xbc
#define	RCQ1C_RxCompletionQ1BaseAddress		0xffffff00
#define	RCQ1C_RxCompletionQ164bitAddress	(1U << 7)
#define	RCQ1C_RxCompletionQ1ProducerWe		(1U << 6)
#define	RCQ1C_RxCompletionQ1Type(x)		((x) << 4)
#define	RCQ1C_RxCompletionQ1Threshold(x)	((x) << 0)

#define	SF_RxCompletionQueue2Ctrl 0xc0
#define	RCQ1C_RxCompletionQ2BaseAddress		0xffffff00
#define	RCQ1C_RxCompletionQ264bitAddress	(1U << 7)
#define	RCQ1C_RxCompletionQ2ProducerWe		(1U << 6)
#define	RCQ1C_RxCompletionQ2Type(x)		((x) << 4)
#define	RCQ1C_RxCompletionQ2Threshold(x)	((x) << 0)

#define	SF_CompletionQueueConsumerIndex 0xc4
#define	CQCI_TxCompletionThresholdMode		(1U << 31)
#define	CQCI_TxCompletionConsumerIndex(x)	((x) << 16)
#define	CQCI_TxCompletionConsumerIndex_get(x)	(((x) >> 16) & 0x7ff)
#define	CQCI_RxCompletionQ1ThresholdMode	(1U << 15)
#define	CQCI_RxCompletionQ1ConsumerIndex(x)	((x) << 0)
#define	CQCI_RxCompletionQ1ConsumerIndex_get(x)	((x) & 0x7ff)

#define	SF_CompletionQueueProducerIndex 0xc8
#define	CQPI_TxCompletionProducerIndex(x)	((x) << 16)
#define	CQPI_TxCompletionProducerIndex_get(x)	(((x) >> 16) & 0x7ff)
#define	CQPI_RxCompletionQ1ProducerIndex(x)	((x) << 0)
#define	CQPI_RxCompletionQ1ProducerIndex_get(x)	((x) & 0x7ff)

#define	SF_RxHiPrCompletionPtrs	0xcc
#define	RHPCP_RxCompletionQ2ProducerIndex(x)	((x) << 16)
#define	RHPCP_RxCompletionQ2ThresholdMode	(1U << 15)
#define	RHPCP_RxCompletionQ2ConsumerIndex(x)	((x) << 0)

#define	SF_RxDmaCtrl		0xd0
#define	RDC_RxReportBadFrames			(1U << 31)
#define	RDC_RxDmaShortFrames			(1U << 30)
#define	RDC_RxDmaBadFrames			(1U << 29)
#define	RDC_RxDmaCrcErrorFrames			(1U << 28)
#define	RDC_RxDmaControlFrame			(1U << 27)
#define	RDC_RxDmaPauseFrame			(1U << 26)
#define	RDC_RxChecksumMode(x)			((x) << 24)
#define	RDC_RxCompletionQ2Enable		(1U << 23)
#define	RDC_RxDmaQueueMode(x)			((x) << 20)
#define	RDC_RxUseBackupQueue			(1U << 19)
#define	RDC_RxDmaCrc				(1U << 18)
#define	RDC_RxEarlyIntThreshold(x)		((x) << 12)
#define	RDC_RxHighPriorityThreshold(x)		((x) << 8)
#define	RDC_RxBurstSize(x)			((x) << 0)

#define	SF_RxDescQueue1Ctrl	0xd4
#define	RDQ1C_RxQ1BufferLength(x)		((x) << 16)
#define	RDQ1C_RxPrefetchDescriptorsMode		(1U << 15)
#define	RDQ1C_RxDescQ1Entries			(1U << 14)
#define	RDQ1C_RxVariableSizeQueues		(1U << 13)
#define	RDQ1C_Rx64bitBufferAddresses		(1U << 12)
#define	RDQ1C_Rx64bitDescQueueAddress		(1U << 11)
#define	RDQ1C_RxDescSpacing(x)			((x) << 8)
#define	RDQ1C_RxQ1ConsumerWe			(1U << 7)
#define	RDQ1C_RxQ1MinDescriptorsThreshold(x)	((x) << 0)

#define	SF_RxDescQueue2Ctrl	0xd8
#define	RDQ2C_RxQ2BufferLength(x)		((x) << 16)
#define	RDQ2C_RxDescQ2Entries			(1U << 14)
#define	RDQ2C_RxQ2MinDescriptorsThreshold(x)	((x) << 0)

#define	SF_RxDescQueueHighAddress 0xdc

#define	SF_RxDescQueue1LowAddress 0xe0

#define	SF_RxDescQueue2LowAddress 0xe4

#define	SF_RxDescQueue1Ptrs	0xe8
#define	RXQ1P_RxDescQ1Consumer(x)		((x) << 16)
#define	RXQ1P_RxDescQ1Producer(x)		((x) << 0)
#define	RXQ1P_RxDescQ1Producer_get(x)		((x) & 0x7ff)

#define	SF_RxDescQueue2Ptrs	0xec
#define	RXQ2P_RxDescQ2Consumer(x)		((x) << 16)
#define	RXQ2P_RxDescQ2Producer(x)		((x) << 0)

#define	SF_RxDmaStatus		0xf0
#define	RDS_RxFramesLostCount(x)		((x) & 0xffff)

#define	SF_RxAddressFilteringCtl 0xf4
#define	RAFC_PerfectAddressPriority(x)		(1U << ((x) + 16))
#define	RAFC_MinVlanPriority(x)			((x) << 13)
#define	RAFC_PassMulticastExceptBroadcast	(1U << 12)
#define	RAFC_WakeupMode(x)			((x) << 10)
#define	RAFC_VlanMode(x)			((x) << 8)
#define	RAFC_PerfectFilteringMode(x)		((x) << 6)
#define	RAFC_HashFilteringMode(x)		((x) << 4)
#define	RAFC_HashPriorityEnable			(1U << 3)
#define	RAFC_PassBroadcast			(1U << 2)
#define	RAFC_PassMulticast			(1U << 1)
#define	RAFC_PromiscuousMode			(1U << 0)

#define	SF_RxFrameTestOut	0xf8

/*
 * Additional PCI registers.  To access these registers via I/O space,
 * indirect access must be used.
 */
#define	SF_PciTargetStatus	0x100

#define	SF_PciMasterStatus1	0x104

#define	SF_PciMasterStatus2	0x108

#define	SF_PciDmaLowHostAddr	0x10c

#define	SF_BacDmaDiagnostic0	0x110

#define	SF_BacDmaDiagnostic1	0x114

#define	SF_BacDmaDiagnostic2	0x118

#define	SF_BacDmaDiagnostic3	0x11c

#define	SF_MacAddr1		0x120

#define	SF_MacAddr2		0x124

#define	SF_FunctionEvent	0x130

#define	SF_FunctionEventMask	0x134

#define	SF_FunctionPresentState	0x138

#define	SF_ForceFunction	0x13c

#define	SF_EEPROM_BASE		0x1000

#define	SF_MII_BASE		0x2000
#define	MiiDataValid		(1U << 31)
#define	MiiBusy			(1U << 30)
#define	MiiRegDataPort(x)	((x) & 0xffff)

#define	SF_MII_PHY_REG(p, r)	(SF_MII_BASE +				\
				 ((p) * 32 * sizeof(uint32_t)) +	\
				 ((r) * sizeof(uint32_t)))

#define	SF_TestMode		0x4000

#define	SF_RxFrameProcessorCtrl	0x4004

#define	SF_TxFrameProcessorCtrl	0x4008

#define	SF_MacConfig1		0x5000
#define	MC1_SoftRst			(1U << 15)
#define	MC1_MiiLoopBack			(1U << 14)
#define	MC1_TestMode(x)			((x) << 12)
#define	MC1_TxFlowEn			(1U << 11)
#define	MC1_RxFlowEn			(1U << 10)
#define	MC1_PreambleDetectCount		(1U << 9)
#define	MC1_PassAllRxPackets		(1U << 8)
#define	MC1_PurePreamble		(1U << 7)
#define	MC1_LengthCheck			(1U << 6)
#define	MC1_NoBackoff			(1U << 5)
#define	MC1_DelayCRC			(1U << 4)
#define	MC1_TxHalfDuplexJam		(1U << 3)
#define	MC1_PadEn			(1U << 2)
#define	MC1_FullDuplex			(1U << 1)
#define	MC1_HugeFrame			(1U << 0)

#define	SF_MacConfig2		0x5004
#define	MC2_TxCRCerr			(1U << 15)
#define	MC2_TxIslCRCerr			(1U << 14)
#define	MC2_RxCRCerr			(1U << 13)
#define	MC2_RxIslCRCerr			(1U << 12)
#define	MC2_TXCF			(1U << 11)
#define	MC2_CtlSoftRst			(1U << 10)
#define	MC2_RxSoftRst			(1U << 9)
#define	MC2_TxSoftRst			(1U << 8)
#define	MC2_RxISLEn			(1U << 7)
#define	MC2_BackPressureNoBackOff	(1U << 6)
#define	MC2_AutoVlanPad			(1U << 5)
#define	MC2_MandatoryVLANPad		(1U << 4)
#define	MC2_TxISLAppen			(1U << 3)
#define	MC2_TxISLEn			(1U << 2)
#define	MC2_SimuRst			(1U << 1)
#define	MC2_TxXmtEn			(1U << 0)

#define	SF_BkToBkIPG		0x5008

#define	SF_NonBkToBkIPG		0x500c

#define	SF_ColRetry		0x5010

#define	SF_MaxLength		0x5014

#define	SF_TxNibbleCnt		0x5018

#define	SF_TxByteCnt		0x501c

#define	SF_ReTxCnt		0x5020

#define	SF_RandomNumGen		0x5024

#define	SF_MskRandomNum		0x5028

#define	SF_TotalTxCnt		0x5034

#define	SF_RxByteCnt		0x5040

#define	SF_TxPauseTimer		0x5060

#define	SF_VLANType		0x5064

#define	SF_MiiStatus		0x5070

#define	SF_PERFECT_BASE		0x6000
#define	SF_PERFECT_SIZE		0x100

#define	SF_HASH_BASE		0x6100
#define	SF_HASH_SIZE		0x200

#define	SF_STATS_BASE		0x7000
struct sf_stats {
	uint32_t	TransmitOKFrames;
	uint32_t	SingleCollisionFrames;
	uint32_t	MultipleCollisionFrames;
	uint32_t	TransmitCRCErrors;
	uint32_t	TransmitOKOctets;
	uint32_t	TransmitDeferredFrames;
	uint32_t	TransmitLateCollisionCount;
	uint32_t	TransmitPauseControlFrames;
	uint32_t	TransmitControlFrames;
	uint32_t	TransmitAbortDueToExcessiveCollisions;
	uint32_t	TransmitAbortDueToExcessingDeferral;
	uint32_t	MulticastFramesTransmittedOK;
	uint32_t	BroadcastFramesTransmittedOK;
	uint32_t	FramesLostDueToInternalTransmitErrors;
	uint32_t	ReceiveOKFrames;
	uint32_t	ReceiveCRCErrors;
	uint32_t	AlignmentErrors;
	uint32_t	ReceiveOKOctets;
	uint32_t	PauseFramesReceivedOK;
	uint32_t	ControlFramesReceivedOK;
	uint32_t	ControlFramesReceivedWithUnsupportedOpcode;
	uint32_t	ReceiveFramesTooLong;
	uint32_t	ReceiveFramesTooShort;
	uint32_t	ReceiveFramesJabbersError;
	uint32_t	ReceiveFramesFragments;
	uint32_t	ReceivePackets64Bytes;
	uint32_t	ReceivePackets127Bytes;
	uint32_t	ReceivePackets255Bytes;
	uint32_t	ReceivePackets511Bytes;
	uint32_t	ReceivePackets1023Bytes;
	uint32_t	ReceivePackets1518Bytes;
	uint32_t	FramesLostDueToInternalReceiveErrors;
	uint32_t	TransmitFifoUnderflowCounts;
};

#define	SF_TxGfpMem		0x8000

#define	SF_RxGfpMem		0xa000

/*
 * Data structure definitions for the Adaptec AIC-6915 (``Starfire'')
 * PCI 10/100 Ethernet controller driver.
 */

/*
 * Transmit descriptor list size.
 */
#define	SF_NTXDESC		256
#define	SF_NTXDESC_MASK		(SF_NTXDESC - 1)
#define	SF_NEXTTX(x)		((x + 1) & SF_NTXDESC_MASK)

/*
 * Transmit completion queue size.  1024 is a hardware requirement.
 */
#define	SF_NTCD			1024
#define	SF_NTCD_MASK		(SF_NTCD - 1)
#define	SF_NEXTTCD(x)		((x + 1) & SF_NTCD_MASK)

/*
 * Receive descriptor list size.
 */
#define	SF_NRXDESC		256
#define	SF_NRXDESC_MASK		(SF_NRXDESC - 1)
#define	SF_NEXTRX(x)		((x + 1) & SF_NRXDESC_MASK)

/*
 * Receive completion queue size.  1024 is a hardware requirement.
 */
#define	SF_NRCD			1024
#define	SF_NRCD_MASK		(SF_NRCD - 1)
#define	SF_NEXTRCD(x)		((x + 1) & SF_NRCD_MASK)

/*
 * Control structures are DMA to the Starfire chip.  We allocate them in
 * a single clump that maps to a single DMA segment to make several things
 * easier.
 */
struct sf_control_data {
	/*
	 * The transmit descriptors.
	 */
	struct sf_txdesc0 scd_txdescs[SF_NTXDESC];

	/*
	 * The transmit completion queue entries.
	 */
	struct sf_tcd scd_txcomp[SF_NTCD];

	/*
	 * The receive buffer descriptors.
	 */
	struct sf_rbd32 scd_rxbufdescs[SF_NRXDESC];

	/*
	 * The receive completion queue entries.
	 */
	struct sf_rcd_full scd_rxcomp[SF_NRCD];
};

#define	SF_CDOFF(x)		offsetof(struct sf_control_data, x)
#define	SF_CDTXDOFF(x)		SF_CDOFF(scd_txdescs[(x)])
#define	SF_CDTXCOFF(x)		SF_CDOFF(scd_txcomp[(x)])
#define	SF_CDRXDOFF(x)		SF_CDOFF(scd_rxbufdescs[(x)])
#define	SF_CDRXCOFF(x)		SF_CDOFF(scd_rxcomp[(x)])

/*
 * Software state for transmit and receive descriptors.
 */
struct sf_descsoft {
	struct mbuf *ds_mbuf;		/* head of mbuf chain */
	bus_dmamap_t ds_dmamap;		/* our DMA map */
};

/*
 * Software state per device.
 */
struct sf_softc {
	struct device sc_dev;		/* generic device information */
	bus_space_tag_t sc_st;		/* bus space tag */
	bus_space_handle_t sc_sh;	/* bus space handle */
	bus_space_handle_t sc_sh_func;	/* sub-handle for func regs */
	bus_dma_tag_t sc_dmat;		/* bus DMA tag */
	struct arpcom sc_arpcom;	/* ethernet common data */
	int sc_iomapped;		/* are we I/O mapped? */
	int sc_flags;			/* misc. flags */

	struct mii_data sc_mii;		/* MII/media information */
	struct timeout sc_mii_timeout;	/* MII callout */

	bus_dmamap_t sc_cddmamap;	/* control data DMA map */
#define	sc_cddma	sc_cddmamap->dm_segs[0].ds_addr

	/*
	 * Software state for transmit and receive descriptors.
	 */
	struct sf_descsoft sc_txsoft[SF_NTXDESC];
	struct sf_descsoft sc_rxsoft[SF_NRXDESC];

	/*
	 * Control data structures.
	 */
	struct sf_control_data *sc_control_data;
#define	sc_txdescs	sc_control_data->scd_txdescs
#define	sc_txcomp	sc_control_data->scd_txcomp
#define	sc_rxbufdescs	sc_control_data->scd_rxbufdescs
#define	sc_rxcomp	sc_control_data->scd_rxcomp

	int	sc_txpending;		/* number of Tx requests pending */

	uint32_t sc_InterruptEn;	/* prototype InterruptEn register */

	uint32_t sc_TransmitFrameCSR;	/* prototype TransmitFrameCSR reg */
	uint32_t sc_TxDescQueueCtrl;	/* prototype TxDescQueueCtrl reg */
	int	sc_txthresh;		/* current Tx threshold */

	uint32_t sc_MacConfig1;		/* prototype MacConfig1 register */

	uint32_t sc_RxAddressFilteringCtl;
};

#define	SF_CDTXDADDR(sc, x)	((sc)->sc_cddma + SF_CDTXDOFF((x)))
#define	SF_CDTXCADDR(sc, x)	((sc)->sc_cddma + SF_CDTXCOFF((x)))
#define	SF_CDRXDADDR(sc, x)	((sc)->sc_cddma + SF_CDRXDOFF((x)))
#define	SF_CDRXCADDR(sc, x)	((sc)->sc_cddma + SF_CDRXCOFF((x)))

#define	SF_CDTXDSYNC(sc, x, ops)					\
	bus_dmamap_sync((sc)->sc_dmat, (sc)->sc_cddmamap,		\
	    SF_CDTXDOFF((x)), sizeof(struct sf_txdesc0), (ops))

#define	SF_CDTXCSYNC(sc, x, ops)					\
	bus_dmamap_sync((sc)->sc_dmat, (sc)->sc_cddmamap,		\
	    SF_CDTXCOFF((x)), sizeof(struct sf_tcd), (ops))

#define	SF_CDRXDSYNC(sc, x, ops)					\
	bus_dmamap_sync((sc)->sc_dmat, (sc)->sc_cddmamap,		\
	    SF_CDRXDOFF((x)), sizeof(struct sf_rbd32), (ops))

#define	SF_CDRXCSYNC(sc, x, ops)					\
	bus_dmamap_sync((sc)->sc_dmat, (sc)->sc_cddmamap,		\
	    SF_CDRXCOFF((x)), sizeof(struct sf_rcd_full), (ops))

#define	SF_INIT_RXDESC(sc, x)						\
do {									\
	struct sf_descsoft *__ds = &sc->sc_rxsoft[(x)];			\
									\
	(sc)->sc_rxbufdescs[(x)].rbd32_addr =				\
	    __ds->ds_dmamap->dm_segs[0].ds_addr | RBD_V;		\
	SF_CDRXDSYNC((sc), (x), BUS_DMASYNC_PREWRITE);			\
} while (/*CONSTCOND*/0)

#ifdef _KERNEL
void	sf_attach(struct sf_softc *);
int	sf_intr(void *);
#endif /* _KERNEL */

#endif /* _DEV_IC_AIC6915_H_ */
