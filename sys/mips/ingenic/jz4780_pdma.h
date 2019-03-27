/*-
 * Copyright (c) 2016 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

/* DMA Channel Registers */
#define	PDMA_DSA(n)	(0x00 + 0x20 * n)	/* Channel n Source Address */
#define	PDMA_DTA(n)	(0x04 + 0x20 * n)	/* Channel n Target Address */
#define	PDMA_DTC(n)	(0x08 + 0x20 * n)	/* Channel n Transfer Count */
#define	PDMA_DRT(n)	(0x0C + 0x20 * n)	/* Channel n Request Source */
#define	 DRT_AUTO	(1 << 3)		/* Auto-request. */
#define	PDMA_DCS(n)	(0x10 + 0x20 * n)	/* Channel n Control/Status */
#define	 DCS_NDES	(1 << 31)		/* Non-descriptor mode. */
#define	 DCS_DES8	(1 << 30)		/* Descriptor 8 Word. */
#define	 DCS_AR		(1 << 4)		/* Address Error. */
#define	 DCS_TT		(1 << 3)		/* Transfer Terminate. */
#define	 DCS_HLT	(1 << 2)		/* DMA halt. */
#define	 DCS_CTE	(1 << 0)		/* Channel transfer enable. */
#define	PDMA_DCM(n)	(0x14 + 0x20 * n)	/* Channel n Command */
#define	 DCM_SAI	(1 << 23) /* Source Address Increment. */
#define	 DCM_DAI	(1 << 22) /* Destination Address Increment. */
#define	 DCM_SP_S	14 /* Source port width. */
#define	 DCM_SP_M	(0x3 << DCM_SP_S)
#define	 DCM_SP_1	(0x1 << DCM_SP_S) /* 1 byte */
#define	 DCM_SP_2	(0x2 << DCM_SP_S) /* 2 bytes */
#define	 DCM_SP_4	(0x0 << DCM_SP_S) /* 4 bytes */
#define	 DCM_DP_S	12 /* Destination port width. */
#define	 DCM_DP_M	(0x3 << DCM_DP_S)
#define	 DCM_DP_1	(0x1 << DCM_DP_S) /* 1 byte */
#define	 DCM_DP_2	(0x2 << DCM_DP_S) /* 2 bytes */
#define	 DCM_DP_4	(0x0 << DCM_DP_S) /* 4 bytes */
#define	 DCM_TSZ_S	8 /* Transfer Data Size of a data unit. */
#define	 DCM_TSZ_M	(0x7 << DCM_TSZ_S)
#define	 DCM_TSZ_A	(0x7 << DCM_TSZ_S) /* Autonomy */
#define	 DCM_TSZ_1	(0x1 << DCM_TSZ_S)
#define	 DCM_TSZ_2	(0x2 << DCM_TSZ_S)
#define	 DCM_TSZ_4	(0x0 << DCM_TSZ_S)
#define	 DCM_TSZ_16	(0x3 << DCM_TSZ_S)
#define	 DCM_TSZ_32	(0x4 << DCM_TSZ_S)
#define	 DCM_TSZ_64	(0x5 << DCM_TSZ_S)
#define	 DCM_TSZ_128	(0x6 << DCM_TSZ_S)
#define	 DCM_TIE	(1 << 1) /* Transfer Interrupt Enable (TIE). */
#define	 DCM_LINK	(1 << 0) /* Descriptor Link Enable. */
#define	PDMA_DDA(n)	(0x18 + 0x20 * n)	/* Channel n Descriptor Address */
#define	PDMA_DSD(n)	(0x1C + 0x20 * n)	/* Channel n Stride Difference */

/* Global Control Registers */
#define	PDMA_DMAC	0x1000	/* DMA Control */
#define	 DMAC_FMSC	(1 << 31)
#define	 DMAC_INTCC_S	17
#define	 DMAC_INTCC_M 	(0x1f << DMAC_INTCC_S)
#define	 DMAC_INTCE	(1 << 16) /* Permit INTC_IRQ to be bound to one of programmable channel. */
#define	 DMAC_HLT	(1 << 3) /* Global halt status */
#define	 DMAC_AR	(1 << 2) /* Global address error status */
#define	 DMAC_DMAE	(1 << 0) /* Enable DMA. */
#define	PDMA_DIRQP	0x1004	/* DMA Interrupt Pending */
#define	PDMA_DDB	0x1008	/* DMA Doorbell */
#define	PDMA_DDS	0x100C	/* DMA Doorbell Set */
#define	PDMA_DIP	0x1010	/* Descriptor Interrupt Pending */
#define	PDMA_DIC	0x1014	/* Descriptor Interrupt Clear */
#define	PDMA_DMACP	0x101C	/* DMA Channel Programmable */
#define	PDMA_DSIRQP	0x1020	/* Channel soft IRQ to MCU */
#define	PDMA_DSIRQM	0x1024	/* Channel soft IRQ mask */
#define	PDMA_DCIRQP	0x1028	/* Channel IRQ to MCU */
#define	PDMA_DCIRQM	0x102C	/* Channel IRQ to MCU mask */
#define	PDMA_DMCS	0x1030	/* MCU Control and Status */
#define	PDMA_DMNMB	0x1034	/* MCU Normal Mailbox */
#define	PDMA_DMSMB	0x1038	/* MCU Security Mailbox */
#define	PDMA_DMINT	0x103C	/* MCU Interrupt */

struct pdma_hwdesc {
	uint32_t dcm;		/* DMA Channel Command */
	uint32_t dsa;		/* DMA Source Address */
	uint32_t dta;		/* DMA Target Address */
	uint32_t dtc;		/* DMA Transfer Counter */
	uint32_t sd;		/* Stride Address */
	uint32_t drt;		/* DMA Request Type */
	uint32_t reserved[2];
};

#define	CHAN_DESC_COUNT	4096
#define	CHAN_DESC_SIZE	(sizeof(struct pdma_hwdesc) * CHAN_DESC_COUNT)
