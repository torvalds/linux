/* $OpenBSD: smmureg.h,v 1.4 2025/08/24 19:49:16 patrick Exp $ */
/*
 * Copyright (c) 2021 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* SMMU v2 */

/* Global Register Space 0 */
#define SMMU_SCR0			0x000
#define  SMMU_SCR0_CLIENTPD			(1 << 0)
#define  SMMU_SCR0_GFRE				(1 << 1)
#define  SMMU_SCR0_GFIE				(1 << 2)
#define  SMMU_SCR0_EXIDENABLE			(1 << 3)
#define  SMMU_SCR0_GCFGFRE			(1 << 4)
#define  SMMU_SCR0_GCFGFIE			(1 << 5)
#define  SMMU_SCR0_USFCFG			(1 << 10)
#define  SMMU_SCR0_VMIDPNE			(1 << 11)
#define  SMMU_SCR0_PTM				(1 << 12)
#define  SMMU_SCR0_FB				(1 << 13)
#define  SMMU_SCR0_BSU_MASK			(0x3 << 14)
#define  SMMU_SCR0_VMID16EN			(1U << 31)
#define SMMU_SCR1			0x004
#define SMMU_SCR2			0x008
#define SMMU_SACR			0x010
#define  SMMU_SACR_MMU500_SMTNMB_TLBEN		(1 << 8)
#define  SMMU_SACR_MMU500_S2CRB_TLBEN		(1 << 10)
#define  SMMU_SACR_MMU500_CACHE_LOCK		(1 << 26)
#define SMMU_IDR0			0x020
#define  SMMU_IDR0_NUMSMRG(x)			(((x) >> 0) & 0xff)
#define  SMMU_IDR0_EXIDS			(1 << 8)
#define  SMMU_IDR0_NUMSIDB(x)			(((x) >> 9) & 0xf)
#define  SMMU_IDR0_BTM				(1 << 13)
#define  SMMU_IDR0_CCTM				(1 << 14)
#define  SMMU_IDR0_EXSMRGS			(1 << 15)
#define  SMMU_IDR0_NUMIRPT(x)			(((x) >> 16) & 0xff)
#define  SMMU_IDR0_PTFS(x)			(((x) >> 24) & 0x3)
#define  SMMU_IDR0_PTFS_AARCH32_SHORT_AND_LONG	0x0
#define  SMMU_IDR0_PTFS_AARCH32_ONLY_LONG	0x1
#define  SMMU_IDR0_PTFS_AARCH32_NO		0x2
#define  SMMU_IDR0_PTFS_AARCH32_RES		0x3
#define  SMMU_IDR0_ATOSNS			(1 << 26)
#define  SMMU_IDR0_SMS				(1 << 27)
#define  SMMU_IDR0_NTS				(1 << 28)
#define  SMMU_IDR0_S2TS				(1 << 29)
#define  SMMU_IDR0_S1TS				(1 << 30)
#define  SMMU_IDR0_SES				(1U << 31)
#define SMMU_IDR1			0x024
#define  SMMU_IDR1_NUMCB(x)			(((x) >> 0) & 0xff)
#define  SMMU_IDR1_NUMSSDNDXB(x)		(((x) >> 8) & 0xf)
#define  SMMU_IDR1_SSDTP(x)			(((x) >> 12) & 0x3)
#define  SMMU_IDR1_SSDTP_UNK			0x0
#define  SMMU_IDR1_SSDTP_IDX_NUMSSDNDXB		0x1
#define  SMMU_IDR1_SSDTP_RES			0x2
#define  SMMU_IDR1_SSDTP_IDX_16BIT		0x3
#define  SMMU_IDR1_SMCD				(1 << 15)
#define  SMMU_IDR1_NUMS2CB(x)			(((x) >> 16) & 0xff)
#define  SMMU_IDR1_HAFDBS(x)			(((x) >> 24) & 0x3)
#define  SMMU_IDR1_HAFDBS_NO			0x0
#define  SMMU_IDR1_HAFDBS_AF			0x1
#define  SMMU_IDR1_HAFDBS_RES			0x2
#define  SMMU_IDR1_HAFDBS_AFDB			0x3
#define  SMMU_IDR1_NUMPAGENDXB(x)		(((x) >> 28) & 0x7)
#define  SMMU_IDR1_PAGESIZE_4K			(0U << 31)
#define  SMMU_IDR1_PAGESIZE_64K			(1U << 31)
#define SMMU_IDR2			0x028
#define  SMMU_IDR2_IAS(x)			(((x) >> 0) & 0xf)
#define  SMMU_IDR2_IAS_32BIT			0x0
#define  SMMU_IDR2_IAS_36BIT			0x1
#define  SMMU_IDR2_IAS_40BIT			0x2
#define  SMMU_IDR2_IAS_42BIT			0x3
#define  SMMU_IDR2_IAS_44BIT			0x4
#define  SMMU_IDR2_IAS_48BIT			0x5
#define  SMMU_IDR2_OAS(x)			(((x) >> 4) & 0xf)
#define  SMMU_IDR2_OAS_32BIT			0x0
#define  SMMU_IDR2_OAS_36BIT			0x1
#define  SMMU_IDR2_OAS_40BIT			0x2
#define  SMMU_IDR2_OAS_42BIT			0x3
#define  SMMU_IDR2_OAS_44BIT			0x4
#define  SMMU_IDR2_OAS_48BIT			0x5
#define  SMMU_IDR2_UBS(x)			(((x) >> 8) & 0xf)
#define  SMMU_IDR2_UBS_32BIT			0x0
#define  SMMU_IDR2_UBS_36BIT			0x1
#define  SMMU_IDR2_UBS_40BIT			0x2
#define  SMMU_IDR2_UBS_42BIT			0x3
#define  SMMU_IDR2_UBS_44BIT			0x4
#define  SMMU_IDR2_UBS_49BIT			0x5
#define  SMMU_IDR2_UBS_64BIT			0xf
#define  SMMU_IDR2_PTFSV8_4KB			(1 << 12)
#define  SMMU_IDR2_PTFSV8_16KB			(1 << 13)
#define  SMMU_IDR2_PTFSV8_64KB			(1 << 14)
#define  SMMU_IDR2_VMID16S			(1 << 15)
#define  SMMU_IDR2_EXNUMSMRG			(((x) >> 16) & 0x7ff)
#define  SMMU_IDR2_E2HS				(1 << 27)
#define  SMMU_IDR2_HADS				(1 << 28)
#define  SMMU_IDR2_COMPINDEXS			(1 << 29)
#define  SMMU_IDR2_DIPANS			(1 << 30)
#define SMMU_IDR3			0x02c
#define SMMU_IDR4			0x030
#define SMMU_IDR5			0x034
#define SMMU_IDR6			0x038
#define SMMU_IDR7			0x03c
#define  SMMU_IDR7_MINOR(x)			(((x) >> 0) & 0xf)
#define  SMMU_IDR7_MAJOR(x)			(((x) >> 4) & 0xf)
#define SMMU_SGFSR			0x048
#define SMMU_SGFSYNR0			0x050
#define SMMU_SGFSYNR1			0x054
#define SMMU_SGFSYNR2			0x058
#define SMMU_TLBIVMID			0x064
#define SMMU_TLBIALLNSNH		0x068
#define SMMU_TLBIALLH			0x06c
#define SMMU_STLBGSYNC			0x070
#define SMMU_STLBGSTATUS		0x074
#define  SMMU_STLBGSTATUS_GSACTIVE		(1 << 0)
#define SMMU_SMR(x)			(0x800 + (x) * 0x4) /* 0 - 127 */
#define  SMMU_SMR_ID_SHIFT			0
#define  SMMU_SMR_ID_MASK			0x7fff
#define  SMMU_SMR_MASK_SHIFT			16
#define  SMMU_SMR_MASK_MASK			0x7fff
#define  SMMU_SMR_VALID				(1U << 31)
#define SMMU_S2CR(x)			(0xc00 + (x) * 0x4) /* 0 - 127 */
#define  SMMU_S2CR_EXIDVALID			(1 << 10)
#define  SMMU_S2CR_TYPE_TRANS			(0 << 16)
#define  SMMU_S2CR_TYPE_BYPASS			(1 << 16)
#define  SMMU_S2CR_TYPE_FAULT			(2 << 16)
#define  SMMU_S2CR_TYPE_MASK			(0x3 << 16)

/* Global Register Space 1 */
#define SMMU_CBAR(x)			(0x000 + (x) * 0x4)
#define  SMMU_CBAR_VMID_SHIFT			0
#define  SMMU_CBAR_BPSHCFG_RES			(0x0 << 8)
#define  SMMU_CBAR_BPSHCFG_OSH			(0x1 << 8)
#define  SMMU_CBAR_BPSHCFG_ISH			(0x2 << 8)
#define  SMMU_CBAR_BPSHCFG_NSH			(0x3 << 8)
#define  SMMU_CBAR_MEMATTR_WB			(0xf << 12)
#define  SMMU_CBAR_TYPE_S2_TRANS		(0x0 << 16)
#define  SMMU_CBAR_TYPE_S1_TRANS_S2_BYPASS	(0x1 << 16)
#define  SMMU_CBAR_TYPE_S1_TRANS_S2_FAULT	(0x2 << 16)
#define  SMMU_CBAR_TYPE_S1_TRANS_S2_TRANS	(0x3 << 16)
#define  SMMU_CBAR_TYPE_MASK			(0x3 << 16)
#define  SMMU_CBAR_IRPTNDX_SHIFT		24
#define SMMU_CBFRSYNRA(x)		(0x400 + (x) * 0x4)
#define SMMU_CBA2R(x)			(0x800 + (x) * 0x4)
#define  SMMU_CBA2R_VA64			(1 << 0)
#define  SMMU_CBA2R_MONC			(1 << 1)
#define  SMMU_CBA2R_VMID16_SHIFT		16

/* Context Bank Format */
#define SMMU_CB_SCTLR			0x000
#define  SMMU_CB_SCTLR_M			(1 << 0)
#define  SMMU_CB_SCTLR_TRE			(1 << 1)
#define  SMMU_CB_SCTLR_AFE			(1 << 2)
#define  SMMU_CB_SCTLR_CFRE			(1 << 5)
#define  SMMU_CB_SCTLR_CFIE			(1 << 6)
#define  SMMU_CB_SCTLR_ASIDPNE			(1 << 12)
#define SMMU_CB_ACTLR			0x004
#define  SMMU_CB_ACTLR_CPRE			(1 << 1)
#define SMMU_CB_TCR2			0x010
#define  SMMU_CB_TCR2_PASIZE_32BIT		(0x0 << 0)
#define  SMMU_CB_TCR2_PASIZE_36BIT		(0x1 << 0)
#define  SMMU_CB_TCR2_PASIZE_40BIT		(0x2 << 0)
#define  SMMU_CB_TCR2_PASIZE_42BIT		(0x3 << 0)
#define  SMMU_CB_TCR2_PASIZE_44BIT		(0x4 << 0)
#define  SMMU_CB_TCR2_PASIZE_48BIT		(0x5 << 0)
#define  SMMU_CB_TCR2_PASIZE_MASK		(0x7 << 0)
#define  SMMU_CB_TCR2_AS			(1 << 4)
#define  SMMU_CB_TCR2_SEP_UPSTREAM		(0x7 << 15)
#define SMMU_CB_TTBR0			0x020
#define SMMU_CB_TTBR1			0x028
#define  SMMU_CB_TTBR_ASID_SHIFT		48
#define SMMU_CB_TCR			0x030
#define  SMMU_CB_TCR_T0SZ(x)			((x) << 0)
#define  SMMU_CB_TCR_EPD0			(1 << 7)
#define  SMMU_CB_TCR_IRGN0_NC			(0x0 << 8)
#define  SMMU_CB_TCR_IRGN0_WBWA			(0x1 << 8)
#define  SMMU_CB_TCR_IRGN0_WT			(0x2 << 8)
#define  SMMU_CB_TCR_IRGN0_WB			(0x3 << 8)
#define  SMMU_CB_TCR_ORGN0_NC			(0x0 << 10)
#define  SMMU_CB_TCR_ORGN0_WBWA			(0x1 << 10)
#define  SMMU_CB_TCR_ORGN0_WT			(0x2 << 10)
#define  SMMU_CB_TCR_ORGN0_WB			(0x3 << 10)
#define  SMMU_CB_TCR_SH0_NSH			(0x0 << 12)
#define  SMMU_CB_TCR_SH0_OSH			(0x2 << 12)
#define  SMMU_CB_TCR_SH0_ISH			(0x3 << 12)
#define  SMMU_CB_TCR_TG0_4KB			(0x0 << 14)
#define  SMMU_CB_TCR_TG0_64KB			(0x1 << 14)
#define  SMMU_CB_TCR_TG0_16KB			(0x2 << 14)
#define  SMMU_CB_TCR_TG0_MASK			(0x3 << 14)
#define  SMMU_CB_TCR_T1SZ(x)			((x) << 16)
#define  SMMU_CB_TCR_EPD1			(1 << 23)
#define  SMMU_CB_TCR_IRGN1_NC			(0x0 << 24)
#define  SMMU_CB_TCR_IRGN1_WBWA			(0x1 << 24)
#define  SMMU_CB_TCR_IRGN1_WT			(0x2 << 24)
#define  SMMU_CB_TCR_IRGN1_WB			(0x3 << 24)
#define  SMMU_CB_TCR_ORGN1_NC			(0x0 << 26)
#define  SMMU_CB_TCR_ORGN1_WBWA			(0x1 << 26)
#define  SMMU_CB_TCR_ORGN1_WT			(0x2 << 26)
#define  SMMU_CB_TCR_ORGN1_WB			(0x3 << 26)
#define  SMMU_CB_TCR_SH1_NSH			(0x0 << 28)
#define  SMMU_CB_TCR_SH1_OSH			(0x2 << 28)
#define  SMMU_CB_TCR_SH1_ISH			(0x3 << 28)
#define  SMMU_CB_TCR_TG1_16KB			(0x1 << 30)
#define  SMMU_CB_TCR_TG1_4KB			(0x2 << 30)
#define  SMMU_CB_TCR_TG1_64KB			(0x3 << 30)
#define  SMMU_CB_TCR_TG1_MASK			(0x3 << 30)
#define  SMMU_CB_TCR_S2_SL0_4KB_L2		(0x0 << 6)
#define  SMMU_CB_TCR_S2_SL0_4KB_L1		(0x1 << 6)
#define  SMMU_CB_TCR_S2_SL0_4KB_L0		(0x2 << 6)
#define  SMMU_CB_TCR_S2_SL0_16KB_L3		(0x0 << 6)
#define  SMMU_CB_TCR_S2_SL0_16KB_L2		(0x1 << 6)
#define  SMMU_CB_TCR_S2_SL0_16KB_L1		(0x2 << 6)
#define  SMMU_CB_TCR_S2_SL0_64KB_L3		(0x0 << 6)
#define  SMMU_CB_TCR_S2_SL0_64KB_L2		(0x1 << 6)
#define  SMMU_CB_TCR_S2_SL0_64KB_L1		(0x2 << 6)
#define  SMMU_CB_TCR_S2_SL0_MASK		(0x3 << 6)
#define  SMMU_CB_TCR_S2_PASIZE_32BIT		(0x0 << 16)
#define  SMMU_CB_TCR_S2_PASIZE_36BIT		(0x1 << 16)
#define  SMMU_CB_TCR_S2_PASIZE_40BIT		(0x2 << 16)
#define  SMMU_CB_TCR_S2_PASIZE_42BIT		(0x3 << 16)
#define  SMMU_CB_TCR_S2_PASIZE_44BIT		(0x4 << 16)
#define  SMMU_CB_TCR_S2_PASIZE_48BIT		(0x5 << 16)
#define  SMMU_CB_TCR_S2_PASIZE_MASK		(0x7 << 16)
#define SMMU_CB_MAIR0			0x038
#define SMMU_CB_MAIR1			0x03c
#define  SMMU_CB_MAIR_MAIR_ATTR(attr, idx)	((attr) << ((idx) * 8))
#define  SMMU_CB_MAIR_DEVICE_nGnRnE		0x00
#define  SMMU_CB_MAIR_DEVICE_nGnRE		0x04
#define  SMMU_CB_MAIR_DEVICE_NC			0x44
#define  SMMU_CB_MAIR_DEVICE_WB			0xff
#define  SMMU_CB_MAIR_DEVICE_WT			0x88
#define SMMU_CB_FSR			0x058
#define  SMMU_CB_FSR_TF				(1 << 1)
#define  SMMU_CB_FSR_AFF			(1 << 2)
#define  SMMU_CB_FSR_PF				(1 << 3)
#define  SMMU_CB_FSR_EF				(1 << 4)
#define  SMMU_CB_FSR_TLBMCF			(1 << 5)
#define  SMMU_CB_FSR_TLBLKF			(1 << 6)
#define  SMMU_CB_FSR_ASF			(1 << 7)
#define  SMMU_CB_FSR_UUT			(1 << 8)
#define  SMMU_CB_FSR_SS				(1 << 30)
#define  SMMU_CB_FSR_MULTI			(1U << 31)
#define  SMMU_CB_FSR_MASK			(SMMU_CB_FSR_TF | \
						 SMMU_CB_FSR_AFF | \
						 SMMU_CB_FSR_PF | \
						 SMMU_CB_FSR_EF | \
						 SMMU_CB_FSR_TLBMCF | \
						 SMMU_CB_FSR_TLBLKF | \
						 SMMU_CB_FSR_ASF | \
						 SMMU_CB_FSR_UUT | \
						 SMMU_CB_FSR_SS | \
						 SMMU_CB_FSR_MULTI)
#define SMMU_CB_FAR			0x060
#define SMMU_CB_FSYNR0			0x068
#define SMMU_CB_IPAFAR			0x070
#define SMMU_CB_TLBIVA			0x600
#define SMMU_CB_TLBIVAA			0x608
#define SMMU_CB_TLBIASID		0x610
#define SMMU_CB_TLBIALL			0x618
#define SMMU_CB_TLBIVAL			0x620
#define SMMU_CB_TLBIVAAL		0x628
#define SMMU_CB_TLBIIPAS2		0x630
#define SMMU_CB_TLBIIPAS2L		0x638
#define SMMU_CB_TLBSYNC			0x7f0
#define SMMU_CB_TLBSTATUS		0x7f4
#define  SMMU_CB_TLBSTATUS_SACTIVE		(1 << 0)

/* SMMU v3 */

#define SMMU_V3_IDR0			0x00
#define  SMMU_V3_IDR0_S2P			(1 << 0)
#define  SMMU_V3_IDR0_S1P			(1 << 1)
#define  SMMU_V3_TTF_AA32			(1 << 2)
#define  SMMU_V3_TTF_AA64			(1 << 3)
#define  SMMU_V3_IDR0_COHACC			(1 << 4)
#define  SMMU_V3_IDR0_ASID16			(1 << 12)
#define  SMMU_V3_IDR0_PRI			(1 << 16)
#define  SMMU_V3_IDR0_VMID16			(1 << 18)
#define  SMMU_V3_IDR0_CD2L			(1 << 19)
#define  SMMU_V3_IDR0_ST_LEVEL(x)		(((x) >> 27) & 0x3)
#define  SMMU_V3_IDR0_ST_LEVEL_1		0x0
#define  SMMU_V3_IDR0_ST_LEVEL_2		0x1
#define SMMU_V3_IDR1			0x04
#define  SMMU_V3_IDR1_SIDSIZE(x)		(((x) >> 0) & 0x3f)
#define  SMMU_V3_IDR1_SSIDSIZE(x)		(((x) >> 6) & 0x1f)
#define  SMMU_V3_IDR1_PRIQS(x)			(((x) >> 11) & 0x1f)
#define  SMMU_V3_IDR1_EVENTQS(x)		(((x) >> 16) & 0x1f)
#define  SMMU_V3_IDR1_CMDQS(x)			(((x) >> 21) & 0x1f)
#define SMMU_V3_IDR2			0x08
#define SMMU_V3_IDR3			0x0c
#define  SMMU_V3_IDR3_STT			(1 << 9)
#define SMMU_V3_IDR4			0x10
#define SMMU_V3_IDR5			0x14
#define  SMMU_V3_IDR5_OAS(x)			(((x) >> 0) & 0x7)
#define  SMMU_V3_IDR5_OAS_32BIT			0x0
#define  SMMU_V3_IDR5_OAS_36BIT			0x1
#define  SMMU_V3_IDR5_OAS_40BIT			0x2
#define  SMMU_V3_IDR5_OAS_42BIT			0x3
#define  SMMU_V3_IDR5_OAS_44BIT			0x4
#define  SMMU_V3_IDR5_OAS_48BIT			0x5
#define  SMMU_V3_IDR5_OAS_52BIT			0x6
#define  SMMU_V3_IDR5_VAX			(1 << 10)
#define SMMU_V3_IIDR			0x018
#define SMMU_V3_AIDR			0x01c
#define SMMU_V3_CR0			0x020
#define  SMMU_V3_CR0_SMMUEN			(1 << 0)
#define  SMMU_V3_CR0_PRIQEN			(1 << 1)
#define  SMMU_V3_CR0_EVENTQEN			(1 << 2)
#define  SMMU_V3_CR0_CMDQEN			(1 << 3)
#define SMMU_V3_CR0ACK			0x24
#define SMMU_V3_CR1			0x28
#define  SMMU_V3_CR1_QUEUE_IC(x)		((x) << 0)
#define  SMMU_V3_CR1_QUEUE_OC(x)		((x) << 2)
#define  SMMU_V3_CR1_QUEUE_SH(x)		((x) << 4)
#define  SMMU_V3_CR1_TABLE_IC(x)		((x) << 6)
#define  SMMU_V3_CR1_TABLE_OC(x)		((x) << 8)
#define  SMMU_V3_CR1_TABLE_SH(x)		((x) << 10)
#define  SMMU_V3_CR1_CACHE_NC			0x0
#define  SMMU_V3_CR1_CACHE_WB			0x1
#define  SMMU_V3_CR1_CACHE_WT			0x2
#define  SMMU_V3_CR1_SHARE_NSH			0x0
#define  SMMU_V3_CR1_SHARE_OSH			0x2
#define  SMMU_V3_CR1_SHARE_ISH			0x3
#define SMMU_V3_CR2			0x2c
#define  SMMU_V3_CR2_E2H			(1 << 0)
#define  SMMU_V3_CR2_RECINVSID			(1 << 1)
#define  SMMU_V3_CR2_PTM			(1 << 2)
#define SMMU_V3_GBPA			0x44
#define  SMMU_V3_GBPA_ABORT			(1 << 20)
#define  SMMU_V3_GBPA_UPDATE			(1U << 31)
#define SMMU_V3_IRQ_CTRL		0x50
#define  SMMU_V3_IRQ_CTRL_GERROR		(1 << 0)
#define  SMMU_V3_IRQ_CTRL_PRIQ			(1 << 1)
#define  SMMU_V3_IRQ_CTRL_EVENTQ		(1 << 2)
#define SMMU_V3_IRQ_CTRLACK		0x54
#define SMMU_V3_GERROR			0x60
#define  SMMU_V3_GERROR_MASK			0x1fd
#define  SMMU_V3_GERROR_CMDQ_ERR		(1 << 0)
#define SMMU_V3_GERRORN			0x64
#define SMMU_V3_GERROR_IRQ_CFG0		0x68
#define SMMU_V3_STRTAB_BASE		0x80
#define  SMMU_V3_STRTAB_BASE_RA			(1ULL << 62)
#define SMMU_V3_STRTAB_BASE_CFG		0x88
#define  SMMU_V3_STRTAB_BASE_CFG_LOG2SIZE(x)	((uint64_t)(x) << 0)
#define  SMMU_V3_STRTAB_BASE_CFG_SPLIT(x)	((x) << 6)
#define  SMMU_V3_STRTAB_BASE_CFG_FMT_L1		(0 << 16)
#define  SMMU_V3_STRTAB_BASE_CFG_FMT_L2		(1 << 16)
#define SMMU_V3_CMDQ_BASE		0x90
#define  SMMU_V3_CMDQ_BASE_RA			(1ULL << 62)
#define  SMMU_V3_CMDQ_BASE_LOG2SIZE(x)		((uint64_t)(x) << 0)
#define SMMU_V3_CMDQ_PROD		0x98
#define SMMU_V3_CMDQ_CONS		0x9c
#define  SMMU_V3_CMDQ_CONS_ERR(x)		(((x) >> 24) & 0x7f)
#define SMMU_V3_EVENTQ_BASE		0xa0
#define  SMMU_V3_EVENTQ_BASE_WA			(1ULL << 62)
#define  SMMU_V3_EVENTQ_BASE_LOG2SIZE(x)	((uint64_t)(x) << 0)
#define SMMU_V3_EVENTQ_PROD		0x100a8
#define SMMU_V3_EVENTQ_CONS		0x100ac
#define SMMU_V3_EVENTQ_IRQ_CFG0		0xb0
#define SMMU_V3_PRIQ_BASE		0xc0
#define  SMMU_V3_PRIQ_BASE_WA			(1ULL << 62)
#define  SMMU_V3_PRIQ_BASE_LOG2SIZE(x)		((uint64_t)(x) << 0)
#define SMMU_V3_PRIQ_PROD		0x100c8
#define SMMU_V3_PRIQ_CONS		0x100cc
#define SMMU_V3_PRIQ_IRQ_CFG0		0xd0

#define SMMU_V3_Q_OVF(p)		((p) & (1U << 31))
#define SMMU_V3_Q_IDX(q, p)		((p) & ((1U << (q)->sq_size_log2) - 1))
#define SMMU_V3_Q_WRP(q, p)		((p) & (1U << (q)->sq_size_log2))

#define SMMU_V3_CMD_CFGI_STE		0x03
#define SMMU_V3_CMD_CFGI_STE_RANGE	0x04
#define SMMU_V3_CMD_CFGI_CD		0x05
#define  SMMU_V3_CMD_CFGI_0_SID(x)		(((uint64_t)(x) & 0xffff) << 32)
#define  SMMU_V3_CMD_CFGI_1_LEAF		(1ULL << 0)
#define  SMMU_V3_CMD_CFGI_1_RANGE(x)		(((uint64_t)(x) & 0x1f) << 0)
#define SMMU_V3_CMD_TLBI_NH_ALL		0x10
#define SMMU_V3_CMD_TLBI_NH_ASID	0x11
#define SMMU_V3_CMD_TLBI_NH_VA		0x12
#define SMMU_V3_CMD_TLBI_EL2_ALL	0x20
#define SMMU_V3_CMD_TLBI_EL2_ASID	0x21
#define SMMU_V3_CMD_TLBI_EL2_VA		0x22
#define SMMU_V3_CMD_TLBI_NSNH_ALL	0x30
#define  SMMU_V3_CMD_TLBI_0_NUM(x)		(((uint64_t)(x) & 0x3f) << 12)
#define  SMMU_V3_CMD_TLBI_0_SCALE(x)		(((uint64_t)(x) & 0x7f) << 20)
#define  SMMU_V3_CMD_TLBI_0_VMID(x)		(((uint64_t)(x) & 0xffff) << 32)
#define  SMMU_V3_CMD_TLBI_0_ASID(x)		(((uint64_t)(x) & 0xffff) << 48)
#define  SMMU_V3_CMD_TLBI_1_LEAF		(1ULL << 0)
#define  SMMU_V3_CMD_TLBI_1_TTL(x)		(((uint64_t)(x) & 0x3) << 8)
#define  SMMU_V3_CMD_TLBI_1_TG(x)		(((uint64_t)(x) & 0x3) << 10)
#define SMMU_V3_CMD_SYNC		0x46
#define  SMMU_V3_CMD_SYNC_0_CS_NONE		(0 << 12)
#define  SMMU_V3_CMD_SYNC_0_CS_IRQ		(1 << 12)
#define  SMMU_V3_CMD_SYNC_0_CS_SEV		(2 << 12)
#define  SMMU_V3_CMD_SYNC_0_MSH_NSH		(0x0 << 22)
#define  SMMU_V3_CMD_SYNC_0_MSH_OSH		(0x2 << 22)
#define  SMMU_V3_CMD_SYNC_0_MSH_ISH		(0x3 << 22)
#define  SMMU_V3_CMD_SYNC_0_MSIATTR_OIWB	(0xf << 24)

#define SMMU_V3_CD_0_TCR_T0SZ(x)	(((uint64_t)(x) & 0x3f) << 0)
#define SMMU_V3_CD_0_TCR_TG0_4KB	(0x0ULL << 6)
#define SMMU_V3_CD_0_TCR_TG0_64KB	(0x1ULL << 6)
#define SMMU_V3_CD_0_TCR_TG0_16KB	(0x2ULL << 6)
#define SMMU_V3_CD_0_TCR_IRGN0_NC	(0x0ULL << 8)
#define SMMU_V3_CD_0_TCR_IRGN0_WBWA	(0x1ULL << 8)
#define SMMU_V3_CD_0_TCR_IRGN0_WT	(0x2ULL << 8)
#define SMMU_V3_CD_0_TCR_IRGN0_WB	(0x3ULL << 8)
#define SMMU_V3_CD_0_TCR_ORGN0_NC	(0x0ULL << 10)
#define SMMU_V3_CD_0_TCR_ORGN0_WBWA	(0x1ULL << 10)
#define SMMU_V3_CD_0_TCR_ORGN0_WT	(0x2ULL << 10)
#define SMMU_V3_CD_0_TCR_ORGN0_WB	(0x3ULL << 10)
#define SMMU_V3_CD_0_TCR_SH0_NSH	(0x0ULL << 12)
#define SMMU_V3_CD_0_TCR_SH0_OSH	(0x2ULL << 12)
#define SMMU_V3_CD_0_TCR_SH0_ISH	(0x3ULL << 12)
#define SMMU_V3_CD_0_TCR_EPD0		(1ULL << 14)
#define SMMU_V3_CD_0_ENDI		(1ULL << 15)
#define SMMU_V3_CD_0_TCR_EPD1		(1ULL << 30)
#define SMMU_V3_CD_0_V			(1ULL << 31)
#define SMMU_V3_CD_0_TCR_IPS_32BIT	(0x0ULL << 32)
#define SMMU_V3_CD_0_TCR_IPS_36BIT	(0x1ULL << 32)
#define SMMU_V3_CD_0_TCR_IPS_40BIT	(0x2ULL << 32)
#define SMMU_V3_CD_0_TCR_IPS_42BIT	(0x3ULL << 32)
#define SMMU_V3_CD_0_TCR_IPS_44BIT	(0x4ULL << 32)
#define SMMU_V3_CD_0_TCR_IPS_48BIT	(0x5ULL << 32)
#define SMMU_V3_CD_0_TCR_IPS_52BIT	(0x6ULL << 32)
#define SMMU_V3_CD_0_TCR_TBI0		(1ULL << 38)
#define SMMU_V3_CD_0_AA64		(1ULL << 41)
#define SMMU_V3_CD_0_TCR_HD		(1ULL << 42)
#define SMMU_V3_CD_0_TCR_HA		(1ULL << 43)
#define SMMU_V3_CD_0_S			(1ULL << 44)
#define SMMU_V3_CD_0_R			(1ULL << 45)
#define SMMU_V3_CD_0_A			(1ULL << 46)
#define SMMU_V3_CD_0_ASET		(1ULL << 47)
#define SMMU_V3_CD_0_ASID(x)		(((uint64_t)(x) & 0xffff) << 48)
#define SMMU_V3_CD_3_MAIR_ATTR(attr, idx) ((uint64_t)(attr) << ((idx) * 8))
#define SMMU_V3_CD_3_MAIR_DEVICE_nGnRnE	0x00
#define SMMU_V3_CD_3_MAIR_DEVICE_nGnRE	0x04
#define SMMU_V3_CD_3_MAIR_DEVICE_NC	0x44
#define SMMU_V3_CD_3_MAIR_DEVICE_WB	0xff
#define SMMU_V3_CD_3_MAIR_DEVICE_WT	0x88

#define SMMU_V3_STE_0_V			(1ULL << 0)
#define SMMU_V3_STE_0_CFG_ABORT		(0ULL << 1)
#define SMMU_V3_STE_0_CFG_BYPASS	(4ULL << 1)
#define SMMU_V3_STE_0_CFG_S1_TRANS	(5ULL << 1)
#define SMMU_V3_STE_0_CFG_S2_TRANS	(6ULL << 1)
#define SMMU_V3_STE_0_CFG_NESTED	(7ULL << 1)
#define SMMU_V3_STE_0_S1FMT_LINEAR	(0ULL << 4)
#define SMMU_V3_STE_0_S1FMT_64K_L2	(2ULL << 4)
#define SMMU_V3_STE_1_S1DSS_TERMINATE	(0ULL << 0)
#define SMMU_V3_STE_1_S1DSS_BYPASS	(1ULL << 0)
#define SMMU_V3_STE_1_S1DSS_SSID0	(2ULL << 0)
#define SMMU_V3_STE_1_S1CIR_NC		(0ULL << 2)
#define SMMU_V3_STE_1_S1CIR_WBRA	(1ULL << 2)
#define SMMU_V3_STE_1_S1CIR_WT		(2ULL << 2)
#define SMMU_V3_STE_1_S1CIR_WB		(3ULL << 2)
#define SMMU_V3_STE_1_S1COR_NC		(0ULL << 4)
#define SMMU_V3_STE_1_S1COR_WBRA	(1ULL << 4)
#define SMMU_V3_STE_1_S1COR_WT		(2ULL << 4)
#define SMMU_V3_STE_1_S1COR_WB		(3ULL << 4)
#define SMMU_V3_STE_1_S1CSH_NSH		(0ULL << 6)
#define SMMU_V3_STE_1_S1CSH_OSH		(2ULL << 6)
#define SMMU_V3_STE_1_S1CSH_ISH		(3ULL << 6)
#define SMMU_V3_STE_1_S2FWB		(1ULL << 25)
#define SMMU_V3_STE_1_S1STALLD		(1ULL << 27)
#define SMMU_V3_STE_1_EATS_ABT		(0ULL << 28)
#define SMMU_V3_STE_1_EATS_TRANS	(1ULL << 28)
#define SMMU_V3_STE_1_EATS_S1CHK	(2ULL << 28)
#define SMMU_V3_STE_1_STRW_NSEL1	(0ULL << 30)
#define SMMU_V3_STE_1_STRW_EL2		(2ULL << 30)
#define SMMU_V3_STE_1_SHCFG_INCOMING	(1ULL << 44)
