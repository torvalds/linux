/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * $FreeBSD$
 */

#include "mvs_if.h"

/* Chip registers */
#define CHIP_PCIEIC		0x1900	/* PCIe Interrupt Cause */
#define CHIP_PCIEIM		0x1910	/* PCIe Interrupt Mask */
#define CHIP_PCIIC		0x1d58	/* PCI Interrupt Cause */
#define CHIP_PCIIM		0x1d5c	/* PCI Interrupt Mask */
#define CHIP_MIC		0x1d60	/* Main Interrupt Cause */
#define CHIP_MIM		0x1d64	/* Main Interrupt Mask */
#define CHIP_SOC_MIC		0x20	/* SoC Main Interrupt Cause */
#define CHIP_SOC_MIM		0x24	/* SoC Main Interrupt Mask */
#define IC_ERR_IRQ		 (1 << 0)	/* shift by (2 * port #) */
#define IC_DONE_IRQ		 (1 << 1)	/* shift by (2 * port #) */
#define IC_HC0			 0x000001ff	/* bits 0-8 = HC0 */
#define IC_HC_SHIFT		 9		/* HC1 shift */
#define IC_HC1			 (IC_HC0 << IC_HC_SHIFT) /* 9-17 = HC1 */
#define IC_ERR_HC0		 0x00000055	/* HC0 ERR_IRQ */
#define IC_DONE_HC0		 0x000000aa	/* HC0 DONE_IRQ */
#define IC_ERR_HC1		 (IC_ERR_HC0 << IC_HC_SHIFT) /* HC1 ERR_IRQ */
#define IC_DONE_HC1		 (IC_DONE_HC0 << IC_HC_SHIFT) /* HC1 DONE_IRQ */
#define IC_HC0_COAL_DONE	 (1 << 8)	/* HC0 IRQ coalescing */
#define IC_HC1_COAL_DONE	 (1 << 17)	/* HC1 IRQ coalescing */
#define IC_PCI_ERR		 (1 << 18)
#define IC_TRAN_COAL_LO_DONE	 (1 << 19)	/* transaction coalescing */
#define IC_TRAN_COAL_HI_DONE	 (1 << 20)	/* transaction coalescing */
#define IC_ALL_PORTS_COAL_DONE	 (1 << 21)	/* GEN_II(E) IRQ coalescing */
#define IC_GPIO_INT		 (1 << 22)
#define IC_SELF_INT		 (1 << 23)
#define IC_TWSI_INT		 (1 << 24)
#define IC_MAIN_RSVD		 (0xfe000000)	/* bits 31-25 */
#define IC_MAIN_RSVD_5		 (0xfff10000)	/* bits 31-19 */
#define IC_MAIN_RSVD_SOC	 (0xfffffec0)	/* bits 31-9, 7-6 */

#define CHIP_SOC_LED		0x2C	/* SoC LED Configuration */

/* Additional mask for SoC devices with less than 4 channels */
#define CHIP_SOC_HC0_MASK(num)	(0xff >> ((4 - (num)) * 2))

/* Chip CCC registers */
#define CHIP_ICC		0x18008
#define CHIP_ICC_ALL_PORTS	 (1 << 4)	/* all ports irq event */
#define CHIP_ICT 		0x180cc
#define CHIP_ITT		0x180d0
#define CHIP_TRAN_COAL_CAUSE_LO	0x18088
#define CHIP_TRAN_COAL_CAUSE_HI	0x1808c

/* Host Controller registers */
#define HC_SIZE			0x10000
#define HC_OFFSET		0x20000
#define HC_BASE(hc)		((hc) * HC_SIZE + HC_OFFSET)

#define HC_CFG			0x0	/* Configuration */
#define HC_CFG_TIMEOUT_MASK	 (0xff << 0)
#define HC_CFG_NODMABS		 (1 << 8)
#define HC_CFG_NOEDMABS		 (1 << 9)
#define HC_CFG_NOPRDBS		 (1 << 10)
#define HC_CFG_TIMEOUTEN	 (1 << 16)	/* Timer Enable */
#define HC_CFG_COALDIS(p)	 (1 << ((p) + 24))/* Coalescing Disable*/
#define HC_RQOP			0x4	/* Request Queue Out-Pointer */
#define HC_RQIP			0x8	/* Response Queue In-Pointer */
#define HC_ICT			0xc	/* Interrupt Coalescing Threshold */
#define HC_ICT_SAICOALT_MASK	 0x000000ff
#define HC_ITT			0x10	/* Interrupt Time Threshold */
#define HC_ITT_SAITMTH_MASK	 0x00ffffff
#define HC_IC			0x14	/* Interrupt Cause */
#define HC_IC_DONE(p)		 (1 << (p))	/* SaCrpb/DMA Done */
#define HC_IC_COAL		 (1 << 4)	/* Intr Coalescing */
#define HC_IC_DEV(p)		 (1 << ((p) + 8)) /* Device Intr */

/* Port registers */
#define PORT_SIZE		0x2000
#define PORT_OFFSET		0x2000
#define PORT_BASE(hc)		((hc) * PORT_SIZE + PORT_OFFSET)

#define EDMA_CFG		0x0	/* Configuration */
#define EDMA_CFG_RESERVED	 (0x1f << 0)	/* Queue len ? */
#define EDMA_CFG_ESATANATVCMDQUE (1 << 5)
#define EDMA_CFG_ERDBSZ		 (1 << 8)
#define EDMA_CFG_EQUE		 (1 << 9)
#define EDMA_CFG_ERDBSZEXT	 (1 << 11)
#define EDMA_CFG_RESERVED2	 (1 << 12)
#define EDMA_CFG_EWRBUFFERLEN	 (1 << 13)
#define EDMA_CFG_EDEVERR	 (1 << 14)
#define EDMA_CFG_EEDMAFBS	 (1 << 16)
#define EDMA_CFG_ECUTTHROUGHEN	 (1 << 17)
#define EDMA_CFG_EEARLYCOMPLETIONEN (1 << 18)
#define EDMA_CFG_EEDMAQUELEN	 (1 << 19)
#define EDMA_CFG_EHOSTQUEUECACHEEN (1 << 22)
#define EDMA_CFG_EMASKRXPM	 (1 << 23)
#define EDMA_CFG_RESUMEDIS	 (1 << 24)
#define EDMA_CFG_EDMAFBS	 (1 << 26)
#define EDMA_T			0x4	/* Timer */
#define EDMA_IEC		0x8	/* Interrupt Error Cause */
#define EDMA_IEM		0xc	/* Interrupt Error Mask */
#define EDMA_IE_EDEVERR		 (1 << 2)	/* EDMA Device Error */
#define EDMA_IE_EDEVDIS		 (1 << 3)	/* EDMA Dev Disconn */
#define EDMA_IE_EDEVCON		 (1 << 4)	/* EDMA Dev Conn */
#define EDMA_IE_SERRINT		 (1 << 5)
#define EDMA_IE_ESELFDIS	 (1 << 7)	/* EDMA Self Disable */
#define EDMA_IE_ETRANSINT	 (1 << 8)	/* Transport Layer */
#define EDMA_IE_EIORDYERR	 (1 << 12)	/* EDMA IORdy Error */
#define EDMA_IE_LINKXERR_SATACRC (1 << 0)	/* SATA CRC error */
#define EDMA_IE_LINKXERR_INTERNALFIFO	(1 << 1)	/* internal FIFO err */
#define EDMA_IE_LINKXERR_LINKLAYERRESET	(1 << 2)
	/* Link Layer is reset by the reception of SYNC primitive from device */
#define EDMA_IE_LINKXERR_OTHERERRORS	(1 << 3)
	/*
	 * Link state errors, coding errors, or running disparity errors occur
	 * during FIS reception.
	 */
#define EDMA_IE_LINKTXERR_FISTXABORTED   (1 << 4)	/* FIS Tx is aborted */
#define EDMA_IE_LINKCTLRXERR(x)		((x) << 13)	/* Link Ctrl Recv Err */
#define EDMA_IE_LINKDATARXERR(x)	((x) << 17)	/* Link Data Recv Err */
#define EDMA_IE_LINKCTLTXERR(x)		((x) << 21)	/* Link Ctrl Tx Error */
#define EDMA_IE_LINKDATATXERR(x)	((x) << 26)	/* Link Data Tx Error */
#define EDMA_IE_TRANSPROTERR		(1U << 31)	/* Transport Proto E */
#define EDMA_IE_TRANSIENT		(EDMA_IE_LINKCTLRXERR(0x0b) | \
					 EDMA_IE_LINKCTLTXERR(0x1f))
							/* Non-fatal Errors */
#define EDMA_REQQBAH		0x10	/* Request Queue Base Address High */
#define EDMA_REQQIP		0x14	/* Request Queue In-Pointer */
#define EDMA_REQQOP		0x18	/* Request Queue Out-Pointer */
#define EDMA_REQQP_ERQQP_SHIFT	 5
#define EDMA_REQQP_ERQQP_MASK	 0x000003e0
#define EDMA_REQQP_ERQQBAP_MASK	 0x00000c00
#define EDMA_REQQP_ERQQBA_MASK	 0xfffff000
#define EDMA_RESQBAH		0x1c	/* Response Queue Base Address High */
#define EDMA_RESQIP		0x20	/* Response Queue In-Pointer */
#define EDMA_RESQOP		0x24	/* Response Queue Out-Pointer */
#define EDMA_RESQP_ERPQP_SHIFT	 3
#define EDMA_RESQP_ERPQP_MASK	 0x000000f8
#define EDMA_RESQP_ERPQBAP_MASK	 0x00000300
#define EDMA_RESQP_ERPQBA_MASK	 0xfffffc00
#define EDMA_CMD		0x28	/* Command */
#define EDMA_CMD_EENEDMA	 (1 << 0)	/* Enable EDMA */
#define EDMA_CMD_EDSEDMA	 (1 << 1)	/* Disable EDMA */
#define EDMA_CMD_EATARST	 (1 << 2)	/* ATA Device Reset */
#define EDMA_CMD_EEDMAFRZ	 (1 << 4)	/* EDMA Freeze */
#define EDMA_TC			0x2c	/* Test Control */
#define EDMA_S			0x30	/* Status */
#define EDMA_S_EDEVQUETAG(s)	 ((s) & 0x0000001f)
#define EDMA_S_EDEVDIR_WRITE	 (0 << 5)
#define EDMA_S_EDEVDIR_READ	 (1 << 5)
#define EDMA_S_ECACHEEMPTY	 (1 << 6)
#define EDMA_S_EDMAIDLE		 (1 << 7)
#define EDMA_S_ESTATE(s)	 (((s) & 0x0000ff00) >> 8)
#define EDMA_S_EIOID(s)		 (((s) & 0x003f0000) >> 16)
#define EDMA_IORT		0x34	/* IORdy Timeout */
#define EDMA_CDT		0x40	/* Command Delay Threshold */
#define EDMA_HC			0x60	/* Halt Condition */
#define EDMA_UNKN_RESD		0x6C	/* Unknown register */
#define EDMA_CQDCQOS(x)		(0x90 + ((x) << 2)
					/* NCQ Done/TCQ Outstanding Status */

/* ATA register defines */
#define ATA_DATA                        0x100	/* (RW) data */
#define ATA_FEATURE                     0x104	/* (W) feature */
#define         ATA_F_DMA                0x01	/* enable DMA */
#define         ATA_F_OVL                0x02	/* enable overlap */
#define ATA_ERROR                       0x104	/* (R) error */
#define         ATA_E_ILI                0x01	/* illegal length */
#define         ATA_E_NM                 0x02	/* no media */
#define         ATA_E_ABORT              0x04	/* command aborted */
#define         ATA_E_MCR                0x08	/* media change request */
#define         ATA_E_IDNF               0x10	/* ID not found */
#define         ATA_E_MC                 0x20	/* media changed */
#define         ATA_E_UNC                0x40	/* uncorrectable data */
#define         ATA_E_ICRC               0x80	/* UDMA crc error */
#define		ATA_E_ATAPI_SENSE_MASK	 0xf0	/* ATAPI sense key mask */
#define ATA_COUNT                       0x108	/* (W) sector count */
#define ATA_IREASON                     0x108   /* (R) interrupt reason */
#define         ATA_I_CMD                0x01	/* cmd (1) | data (0) */
#define         ATA_I_IN                 0x02	/* read (1) | write (0) */
#define         ATA_I_RELEASE            0x04	/* released bus (1) */
#define         ATA_I_TAGMASK            0xf8	/* tag mask */
#define ATA_SECTOR                      0x10c	/* (RW) sector # */
#define ATA_CYL_LSB                     0x110	/* (RW) cylinder# LSB */
#define ATA_CYL_MSB                     0x114	/* (RW) cylinder# MSB */
#define ATA_DRIVE                       0x118	/* (W) Sector/Drive/Head */
#define         ATA_D_LBA                0x40	/* use LBA addressing */
#define         ATA_D_IBM                0xa0	/* 512 byte sectors, ECC */
#define ATA_COMMAND                     0x11c	/* (W) command */
#define ATA_STATUS                      0x11c	/* (R) status */
#define         ATA_S_ERROR              0x01	/* error */
#define         ATA_S_INDEX              0x02	/* index */
#define         ATA_S_CORR               0x04	/* data corrected */
#define         ATA_S_DRQ                0x08	/* data request */
#define         ATA_S_DSC                0x10	/* drive seek completed */
#define         ATA_S_SERVICE            0x10	/* drive needs service */
#define         ATA_S_DWF                0x20	/* drive write fault */
#define         ATA_S_DMA                0x20	/* DMA ready */
#define         ATA_S_READY              0x40	/* drive ready */
#define         ATA_S_BUSY               0x80	/* busy */
#define ATA_CONTROL                     0x120	/* (W) control */
#define         ATA_A_IDS                0x02	/* disable interrupts */
#define         ATA_A_RESET              0x04	/* RESET controller */
#define         ATA_A_4BIT               0x08	/* 4 head bits */
#define         ATA_A_HOB                0x80	/* High Order Byte enable */
#define ATA_ALTSTAT                     0x120	/* (R) alternate status */
#define ATAPI_P_READ                    (ATA_S_DRQ | ATA_I_IN)
#define ATAPI_P_WRITE                   (ATA_S_DRQ)
#define ATAPI_P_CMDOUT                  (ATA_S_DRQ | ATA_I_CMD)
#define ATAPI_P_DONEDRQ                 (ATA_S_DRQ | ATA_I_CMD | ATA_I_IN)
#define ATAPI_P_DONE                    (ATA_I_CMD | ATA_I_IN)
#define ATAPI_P_ABORT                   0

/* Basic DMA Registers */
#define DMA_C				0x224	/* Basic DMA Command */
#define DMA_C_START			 (1 << 0)
#define DMA_C_READ			 (1 << 3)
#define DMA_C_DREGIONVALID		 (1 << 8)
#define DMA_C_DREGIONLAST		 (1 << 9)
#define DMA_C_CONTFROMPREV		 (1 << 10)
#define DMA_C_DRBC(n)			 (((n) & 0xffff) << 16)
#define DMA_S				0x228	/* Basic DMA Status */
#define DMA_S_ACT			 (1 << 0) /* Active */
#define DMA_S_ERR			 (1 << 1) /* Error */
#define DMA_S_PAUSED			 (1 << 2) /* Paused */
#define DMA_S_LAST			 (1 << 3) /* Last */
#define DMA_DTLBA			0x22c	/* Descriptor Table Low Base Address */
#define DMA_DTLBA_MASK			 0xfffffff0
#define DMA_DTHBA			0x230	/* Descriptor Table High Base Address */
#define DMA_DRLA			0x234	/* Data Region Low Address */
#define DMA_DRHA			0x238	/* Data Region High Address */

/* Serial-ATA Registers */
#define SATA_SS				0x300	/* SStatus */
#define        SATA_SS_DET_MASK         0x0000000f
#define        SATA_SS_DET_NO_DEVICE    0x00000000
#define        SATA_SS_DET_DEV_PRESENT  0x00000001
#define        SATA_SS_DET_PHY_ONLINE   0x00000003
#define        SATA_SS_DET_PHY_OFFLINE  0x00000004

#define        SATA_SS_SPD_MASK         0x000000f0
#define        SATA_SS_SPD_NO_SPEED     0x00000000
#define        SATA_SS_SPD_GEN1         0x00000010
#define        SATA_SS_SPD_GEN2         0x00000020
#define        SATA_SS_SPD_GEN3         0x00000030

#define        SATA_SS_IPM_MASK         0x00000f00
#define        SATA_SS_IPM_NO_DEVICE    0x00000000
#define        SATA_SS_IPM_ACTIVE       0x00000100
#define        SATA_SS_IPM_PARTIAL      0x00000200
#define        SATA_SS_IPM_SLUMBER      0x00000600
#define SATA_SE				0x304	/* SError */
#define SATA_SEIM			0x340	/* SError Interrupt Mask */
#define        SATA_SE_DATA_CORRECTED   0x00000001
#define        SATA_SE_COMM_CORRECTED   0x00000002
#define        SATA_SE_DATA_ERR         0x00000100
#define        SATA_SE_COMM_ERR         0x00000200
#define        SATA_SE_PROT_ERR         0x00000400
#define        SATA_SE_HOST_ERR         0x00000800
#define        SATA_SE_PHY_CHANGED      0x00010000
#define        SATA_SE_PHY_IERROR       0x00020000
#define        SATA_SE_COMM_WAKE        0x00040000
#define        SATA_SE_DECODE_ERR       0x00080000
#define        SATA_SE_PARITY_ERR       0x00100000
#define        SATA_SE_CRC_ERR          0x00200000
#define        SATA_SE_HANDSHAKE_ERR    0x00400000
#define        SATA_SE_LINKSEQ_ERR      0x00800000
#define        SATA_SE_TRANSPORT_ERR    0x01000000
#define        SATA_SE_UNKNOWN_FIS      0x02000000
#define SATA_SC				0x308	/* SControl */
#define        SATA_SC_DET_MASK         0x0000000f
#define        SATA_SC_DET_IDLE         0x00000000
#define        SATA_SC_DET_RESET        0x00000001
#define        SATA_SC_DET_DISABLE      0x00000004

#define        SATA_SC_SPD_MASK         0x000000f0
#define        SATA_SC_SPD_NO_SPEED     0x00000000
#define        SATA_SC_SPD_SPEED_GEN1   0x00000010
#define        SATA_SC_SPD_SPEED_GEN2   0x00000020
#define        SATA_SC_SPD_SPEED_GEN3   0x00000030

#define        SATA_SC_IPM_MASK         0x00000f00
#define        SATA_SC_IPM_NONE         0x00000000
#define        SATA_SC_IPM_DIS_PARTIAL  0x00000100
#define        SATA_SC_IPM_DIS_SLUMBER  0x00000200

#define        SATA_SC_SPM_MASK		0x0000f000
#define        SATA_SC_SPM_NONE		0x00000000
#define        SATA_SC_SPM_PARTIAL	0x00001000
#define        SATA_SC_SPM_SLUMBER	0x00002000
#define        SATA_SC_SPM_ACTIVE	0x00004000
#define SATA_LTM			0x30c	/* LTMode */
#define SATA_PHYM3			0x310	/* PHY Mode 3 */
#define SATA_PHYM4			0x314	/* PHY Mode 4 */
#define SATA_PHYM1			0x32c	/* PHY Mode 1 */
#define SATA_PHYM2			0x330	/* PHY Mode 2 */
#define SATA_BISTC			0x334	/* BIST Control */
#define SATA_BISTDW1			0x338	/* BIST DW1 */
#define SATA_BISTDW2			0x33c	/* BIST DW2 */
#define SATA_SATAICFG			0x050	/* Serial-ATA Interface Configuration */
#define SATA_SATAICFG_REFCLKCNF_20MHZ	 (0 << 0)
#define SATA_SATAICFG_REFCLKCNF_25MHZ	 (1 << 0)
#define SATA_SATAICFG_REFCLKCNF_30MHZ	 (2 << 0)
#define SATA_SATAICFG_REFCLKCNF_40MHZ	 (3 << 0)
#define SATA_SATAICFG_REFCLKCNF_MASK	 (3 << 0)
#define SATA_SATAICFG_REFCLKDIV_1	 (0 << 2)
#define SATA_SATAICFG_REFCLKDIV_2	 (1 << 2)	/* Used 20 or 25MHz */
#define SATA_SATAICFG_REFCLKDIV_4	 (2 << 2)	/* Used 40MHz */
#define SATA_SATAICFG_REFCLKDIV_3	 (3 << 2)	/* Used 30MHz */
#define SATA_SATAICFG_REFCLKDIV_MASK	 (3 << 2)
#define SATA_SATAICFG_REFCLKFEEDDIV_50	 (0 << 4)	/* or 100, when Gen2En is 1 */
#define SATA_SATAICFG_REFCLKFEEDDIV_60	 (1 << 4)	/* or 120. Used 25MHz */
#define SATA_SATAICFG_REFCLKFEEDDIV_75	 (2 << 4)	/* or 150. Used 20MHz */
#define SATA_SATAICFG_REFCLKFEEDDIV_90	 (3 << 4)	/* or 180 */
#define SATA_SATAICFG_REFCLKFEEDDIV_MASK (3 << 4)
#define SATA_SATAICFG_PHYSSCEN		 (1 << 6)
#define SATA_SATAICFG_GEN2EN		 (1 << 7)
#define SATA_SATAICFG_COMMEN		 (1 << 8)
#define SATA_SATAICFG_PHYSHUTDOWN	 (1 << 9)
#define SATA_SATAICFG_TARGETMODE	 (1 << 10)	/* 1 = Initiator */
#define SATA_SATAICFG_COMCHANNEL	 (1 << 11)
#define SATA_SATAICFG_IGNOREBSY		 (1 << 24)
#define SATA_SATAICFG_LINKRSTEN		 (1 << 25)
#define SATA_SATAICFG_CMDRETXDS		 (1 << 26)
#define SATA_SATAICTL			0x344	/* Serial-ATA Interface Control */
#define SATA_SATAICTL_PMPTX_MASK	 0x0000000f
#define SATA_SATAICTL_PMPTX_SHIFT	 0
#define SATA_SATAICTL_VUM		 (1 << 8)
#define SATA_SATAICTL_VUS		 (1 << 9)
#define SATA_SATAICTL_EDMAACT		 (1 << 16)
#define SATA_SATAICTL_CLEARSTAT		 (1 << 24)
#define SATA_SATAICTL_SRST		 (1 << 25)
#define SATA_SATAITC			0x348	/* Serial-ATA Interface Test Control */
#define SATA_SATAIS			0x34c	/* Serial-ATA Interface Status */
#define SATA_VU				0x35c	/* Vendor Unique */
#define SATA_FISC			0x360	/* FIS Configuration */
#define SATA_FISC_FISWAIT4RDYEN_B0	 (1 << 0) /* Device to Host FIS */
#define SATA_FISC_FISWAIT4RDYEN_B1	 (1 << 1) /* SDB FIS rcv with <N>bit 0 */
#define SATA_FISC_FISWAIT4RDYEN_B2	 (1 << 2) /* DMA Activate FIS */
#define SATA_FISC_FISWAIT4RDYEN_B3	 (1 << 3) /* DMA Setup FIS */
#define SATA_FISC_FISWAIT4RDYEN_B4	 (1 << 4) /* Data FIS first DW */
#define SATA_FISC_FISWAIT4RDYEN_B5	 (1 << 5) /* Data FIS entire FIS */
#define SATA_FISC_FISWAIT4HOSTRDYEN_B0	 (1 << 8)
				/* Device to Host FIS with <ERR> or <DF> */
#define SATA_FISC_FISWAIT4HOSTRDYEN_B1	 (1 << 9) /* SDB FIS rcv with <N>bit */
#define SATA_FISC_FISWAIT4HOSTRDYEN_B2	 (1 << 10) /* SDB FIS rcv with <ERR> */
#define SATA_FISC_FISWAIT4HOSTRDYEN_B3	 (1 << 11) /* BIST Acivate FIS */
#define SATA_FISC_FISWAIT4HOSTRDYEN_B4	 (1 << 12) /* PIO Setup FIS */
#define SATA_FISC_FISWAIT4HOSTRDYEN_B5	 (1 << 13) /* Data FIS with Link error */
#define SATA_FISC_FISWAIT4HOSTRDYEN_B6	 (1 << 14) /* Unrecognized FIS type */
#define SATA_FISC_FISWAIT4HOSTRDYEN_B7	 (1 << 15) /* Any FIS */
#define SATA_FISC_FISDMAACTIVATESYNCRESP (1 << 16)
#define SATA_FISC_FISUNRECTYPECONT	 (1 << 17)
#define SATA_FISIC			0x364	/* FIS Interrupt Cause */
#define SATA_FISIM			0x368	/* FIS Interrupt Mask */
#define SATA_FISDW0			0x370	/* FIS DW0 */
#define SATA_FISDW1			0x374	/* FIS DW1 */
#define SATA_FISDW2			0x378	/* FIS DW2 */
#define SATA_FISDW3			0x37c	/* FIS DW3 */
#define SATA_FISDW4			0x380	/* FIS DW4 */
#define SATA_FISDW5			0x384	/* FIS DW5 */
#define SATA_FISDW6			0x388	/* FIS DW6 */

#define SATA_PHYM9_GEN2			0x398
#define SATA_PHYM9_GEN1			0x39c
#define SATA_PHYCFG_OFS			0x3a0	/* 65nm SoCs only */

#define MVS_MAX_PORTS			8
#define MVS_MAX_SLOTS			32

/* Pessimistic prognosis on number of required S/G entries */
#define MVS_SG_ENTRIES		(btoc(MAXPHYS) + 1)

/* EDMA Command Request Block (CRQB) Data */
struct mvs_crqb {
	uint32_t cprdbl;	/* cPRD Desriptor Table Base Low Address */
	uint32_t cprdbh;	/* cPRD Desriptor Table Base High Address */
	uint16_t ctrlflg;	/* Control Flags */
#define MVS_CRQB_READ		0x0001
#define MVS_CRQB_TAG_MASK	0x003e
#define MVS_CRQB_TAG_SHIFT	1
#define MVS_CRQB_PMP_MASK	0xf000
#define MVS_CRQB_PMP_SHIFT	12
	uint8_t cmd[22];
} __packed;

struct mvs_crqb_gen2e {
	uint32_t cprdbl;	/* cPRD Desriptor Table Base Low Address */
	uint32_t cprdbh;	/* cPRD Desriptor Table Base High Address */
	uint32_t ctrlflg;	/* Control Flags */
#define MVS_CRQB2E_READ		0x00000001
#define MVS_CRQB2E_DTAG_MASK	0x0000003e
#define MVS_CRQB2E_DTAG_SHIFT	1
#define MVS_CRQB2E_PMP_MASK	0x0000f000
#define MVS_CRQB2E_PMP_SHIFT	12
#define MVS_CRQB2E_CPRD		0x00010000
#define MVS_CRQB2E_HTAG_MASK	0x003e0000
#define MVS_CRQB2E_HTAG_SHIFT	17
	uint32_t drbc;		/* Data Region Byte Count */
	uint8_t cmd[16];
} __packed;

/* EDMA Phisical Region Descriptors (ePRD) Table Data Structure */
struct mvs_eprd {
	uint32_t prdbal;	/* Address bits[31:1] */
	uint32_t bytecount;	/* Byte Count */
#define MVS_EPRD_MASK		0x0000ffff      /* max 64KB */
#define MVS_EPRD_MAX		(MVS_EPRD_MASK + 1)
#define MVS_EPRD_EOF		0x80000000
	uint32_t prdbah;	/* Address bits[63:32] */
	uint32_t resv;
} __packed;

/* Command request blocks. 32 commands. First 1Kbyte aligned. */
#define MVS_CRQB_OFFSET		0
#define MVS_CRQB_SIZE		32	/* sizeof(struct mvs_crqb) */
#define MVS_CRQB_MASK		0x000003e0
#define MVS_CRQB_SHIFT		5
#define MVS_CRQB_TO_ADDR(slot)	((slot) << MVS_CRQB_SHIFT)
#define MVS_ADDR_TO_CRQB(addr)	(((addr) & MVS_CRQB_MASK) >> MVS_CRQB_SHIFT)
/* ePRD blocks. Up to 32 commands, Each 16byte aligned. */
#define MVS_EPRD_OFFSET		(MVS_CRQB_OFFSET + MVS_CRQB_SIZE * MVS_MAX_SLOTS)
#define MVS_EPRD_SIZE		(MVS_SG_ENTRIES * 16) /* sizeof(struct mvs_eprd) */
/* Request work area. */
#define MVS_WORKRQ_SIZE		(MVS_EPRD_OFFSET + MVS_EPRD_SIZE * MVS_MAX_SLOTS)

/* EDMA Command Response Block (CRPB) Data */
struct mvs_crpb {
	uint16_t id;		/* CRPB ID */
#define MVS_CRPB_TAG_MASK	0x001F
#define MVS_CRPB_TAG_SHIFT	0
	uint16_t rspflg;	/* CPRB Response Flags */
#define MVS_CRPB_EDMASTS_MASK	0x007F
#define MVS_CRPB_EDMASTS_SHIFT	0
#define MVS_CRPB_ATASTS_MASK	0xFF00
#define MVS_CRPB_ATASTS_SHIFT	8
	uint32_t ts;		/* CPRB Time Stamp */
} __packed;

/* Command response blocks. 32 commands. First 256byte aligned. */
#define MVS_CRPB_OFFSET		0
#define MVS_CRPB_SIZE		sizeof(struct mvs_crpb)
#define MVS_CRPB_MASK		0x000000f8
#define MVS_CRPB_SHIFT		3
#define MVS_CRPB_TO_ADDR(slot)	((slot) << MVS_CRPB_SHIFT)
#define MVS_ADDR_TO_CRPB(addr)	(((addr) & MVS_CRPB_MASK) >> MVS_CRPB_SHIFT)
/* Request work area. */
#define MVS_WORKRP_SIZE		(MVS_CRPB_OFFSET + MVS_CRPB_SIZE * MVS_MAX_SLOTS)

/* misc defines */
#define ATA_IRQ_RID		0
#define ATA_INTR_FLAGS		(INTR_MPSAFE|INTR_TYPE_BIO|INTR_ENTROPY)

struct ata_dmaslot {
    bus_dmamap_t                data_map;       /* Data DMA map */
    bus_addr_t			addr;		/* Data address */
    uint16_t			len;		/* Data size */
};

/* structure holding DMA related information */
struct mvs_dma {
    bus_dma_tag_t               workrq_tag;	/* Request workspace DMA tag */
    bus_dmamap_t                workrq_map;	/* Request workspace DMA map */
    uint8_t                     *workrq;	/* Request workspace */
    bus_addr_t                  workrq_bus;	/* Request bus address */
    bus_dma_tag_t               workrp_tag;	/* Reply workspace DMA tag */
    bus_dmamap_t                workrp_map;	/* Reply workspace DMA map */
    uint8_t                     *workrp;	/* Reply workspace */
    bus_addr_t                  workrp_bus;	/* Reply bus address */
    bus_dma_tag_t               data_tag;	/* Data DMA tag */
};

enum mvs_slot_states {
	MVS_SLOT_EMPTY,
	MVS_SLOT_LOADING,
	MVS_SLOT_RUNNING,
	MVS_SLOT_EXECUTING
};

struct mvs_slot {
    device_t                    dev;            /* Device handle */
    int				slot;           /* Number of this slot */
    int				tag;            /* Used command tag */
    enum mvs_slot_states	state;          /* Slot state */
    union ccb			*ccb;		/* CCB occupying slot */
    struct ata_dmaslot          dma;            /* DMA data of this slot */
    struct callout              timeout;        /* Execution timeout */
};

struct mvs_device {
	int			revision;
	int			mode;
	u_int			bytecount;
	u_int			atapi;
	u_int			tags;
	u_int			caps;
};

enum mvs_edma_mode {
	MVS_EDMA_UNKNOWN,
	MVS_EDMA_OFF,
	MVS_EDMA_ON,
	MVS_EDMA_QUEUED,
	MVS_EDMA_NCQ,
};

/* structure describing an ATA channel */
struct mvs_channel {
	device_t		dev;            /* Device handle */
	int			unit;           /* Physical channel */
	struct resource		*r_mem;		/* Memory of this channel */
	struct resource		*r_irq;         /* Interrupt of this channel */
	void			*ih;            /* Interrupt handle */
	struct mvs_dma		dma;            /* DMA data */
	struct cam_sim		*sim;
	struct cam_path		*path;
	int			quirks;
#define MVS_Q_GENI	1
#define MVS_Q_GENII	2
#define MVS_Q_GENIIE	4
#define MVS_Q_SOC	8
#define MVS_Q_CT	16
#define MVS_Q_SOC65	32
	int			pm_level;	/* power management level */

	struct mvs_slot		slot[MVS_MAX_SLOTS];
	union ccb		*hold[MVS_MAX_SLOTS];
	int			holdtag[MVS_MAX_SLOTS]; /* Tags used for held commands. */
	struct mtx		mtx;		/* state lock */
	int			devices;        /* What is present */
	int			pm_present;	/* PM presence reported */
	enum mvs_edma_mode	curr_mode;	/* Current EDMA mode */
	int			fbs_enabled;	/* FIS-based switching enabled */
	uint32_t		oslots;		/* Occupied slots */
	uint32_t		otagspd[16];	/* Occupied device tags */
	uint32_t		rslots;		/* Running slots */
	uint32_t		aslots;		/* Slots with atomic commands  */
	uint32_t		eslots;		/* Slots in error */
	uint32_t		toslots;	/* Slots in timeout */
	int			numrslots;	/* Number of running slots */
	int			numrslotspd[16];/* Number of running slots per dev */
	int			numpslots;	/* Number of PIO slots */
	int			numdslots;	/* Number of DMA slots */
	int			numtslots;	/* Number of NCQ slots */
	int			numtslotspd[16];/* Number of NCQ slots per dev */
	int			numhslots;	/* Number of held slots */
	int			recoverycmd;	/* Our READ LOG active */
	int			fatalerr;	/* Fatal error happened */
	int			lastslot;	/* Last used slot */
	int			taggedtarget;	/* Last tagged target */
	int			resetting;	/* Hard-reset in progress. */
	int			resetpolldiv;	/* Hard-reset poll divider. */
	int			out_idx;	/* Next written CRQB */
	int			in_idx;		/* Next read CRPB */
	u_int			transfersize;	/* PIO transfer size */
	u_int			donecount;	/* PIO bytes sent/received */
	u_int			basic_dma;	/* Basic DMA used for ATAPI */
	u_int			fake_busy;	/* Fake busy bit after command submission */
	union ccb		*frozen;	/* Frozen command */
	struct callout		pm_timer;	/* Power management events */
	struct callout		reset_timer;	/* Hard-reset timeout */

	struct mvs_device	user[16];	/* User-specified settings */
	struct mvs_device	curr[16];	/* Current settings */
};

/* structure describing a MVS controller */
struct mvs_controller {
	device_t		dev;
	int			r_rid;
	struct resource		*r_mem;
	struct rman		sc_iomem;
	struct mvs_controller_irq {
		struct resource		*r_irq;
		void			*handle;
		int			r_irq_rid;
	} irq;
	int			quirks;
	int			channels;
	int			ccc;		/* CCC timeout */
	int			cccc;		/* CCC commands */
	struct mtx		mtx;		/* MIM access lock */
	int			gmim;		/* Globally wanted MIM bits */
	int			pmim;		/* Port wanted MIM bits */
	int			mim;		/* Current MIM bits */
	int			msi;		/* MSI enabled */
	int			msia;		/* MSI active */
	struct {
		void			(*function)(void *);
		void			*argument;
	} interrupt[MVS_MAX_PORTS];
};

enum mvs_err_type {
	MVS_ERR_NONE,		/* No error */
	MVS_ERR_INVALID,	/* Error detected by us before submitting. */
	MVS_ERR_INNOCENT,	/* Innocent victim. */
	MVS_ERR_TFE,		/* Task File Error. */
	MVS_ERR_SATA,		/* SATA error. */
	MVS_ERR_TIMEOUT,	/* Command execution timeout. */
	MVS_ERR_NCQ,		/* NCQ command error. CCB should be put on hold
				 * until READ LOG executed to reveal error. */
};

struct mvs_intr_arg {
	void *arg;
	u_int cause;
};

extern devclass_t mvs_devclass;

/* macros to hide busspace uglyness */
#define ATA_INB(res, offset) \
	bus_read_1((res), (offset))
#define ATA_INW(res, offset) \
	bus_read_2((res), (offset))
#define ATA_INL(res, offset) \
	bus_read_4((res), (offset))
#define ATA_INSW(res, offset, addr, count) \
	bus_read_multi_2((res), (offset), (addr), (count))
#define ATA_INSW_STRM(res, offset, addr, count) \
	bus_read_multi_stream_2((res), (offset), (addr), (count))
#define ATA_INSL(res, offset, addr, count) \
	bus_read_multi_4((res), (offset), (addr), (count))
#define ATA_INSL_STRM(res, offset, addr, count) \
	bus_read_multi_stream_4((res), (offset), (addr), (count))
#define ATA_OUTB(res, offset, value) \
	bus_write_1((res), (offset), (value))
#define ATA_OUTW(res, offset, value) \
	bus_write_2((res), (offset), (value))
#define ATA_OUTL(res, offset, value) \
	bus_write_4((res), (offset), (value));
#define ATA_OUTSW(res, offset, addr, count) \
	bus_write_multi_2((res), (offset), (addr), (count))
#define ATA_OUTSW_STRM(res, offset, addr, count) \
	bus_write_multi_stream_2((res), (offset), (addr), (count))
#define ATA_OUTSL(res, offset, addr, count) \
	bus_write_multi_4((res), (offset), (addr), (count))
#define ATA_OUTSL_STRM(res, offset, addr, count) \
	bus_write_multi_stream_4((res), (offset), (addr), (count))
