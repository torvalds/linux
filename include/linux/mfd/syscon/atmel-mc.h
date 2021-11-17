/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2005 Ivan Kokshaysky
 * Copyright (C) SAN People
 *
 * Memory Controllers (MC, EBI, SMC, SDRAMC, BFC) - System peripherals
 * registers.
 * Based on AT91RM9200 datasheet revision E.
 */

#ifndef _LINUX_MFD_SYSCON_ATMEL_MC_H_
#define _LINUX_MFD_SYSCON_ATMEL_MC_H_

/* Memory Controller */
#define AT91_MC_RCR			0x00
#define AT91_MC_RCB			BIT(0)

#define AT91_MC_ASR			0x04
#define AT91_MC_UNADD			BIT(0)
#define AT91_MC_MISADD			BIT(1)
#define AT91_MC_ABTSZ			GENMASK(9, 8)
#define AT91_MC_ABTSZ_BYTE		(0 << 8)
#define AT91_MC_ABTSZ_HALFWORD		(1 << 8)
#define AT91_MC_ABTSZ_WORD		(2 << 8)
#define AT91_MC_ABTTYP			GENMASK(11, 10)
#define AT91_MC_ABTTYP_DATAREAD		(0 << 10)
#define AT91_MC_ABTTYP_DATAWRITE	(1 << 10)
#define AT91_MC_ABTTYP_FETCH		(2 << 10)
#define AT91_MC_MST(n)			BIT(16 + (n))
#define AT91_MC_SVMST(n)		BIT(24 + (n))

#define AT91_MC_AASR			0x08

#define AT91_MC_MPR			0x0c
#define AT91_MPR_MSTP(n)		GENMASK(2 + ((x) * 4), ((x) * 4))

/* External Bus Interface (EBI) registers */
#define AT91_MC_EBI_CSA			0x60
#define AT91_MC_EBI_CS(n)		BIT(x)
#define AT91_MC_EBI_NUM_CS		8

#define AT91_MC_EBI_CFGR		0x64
#define AT91_MC_EBI_DBPUC		BIT(0)

/* Static Memory Controller (SMC) registers */
#define AT91_MC_SMC_CSR(n)		(0x70 + ((n) * 4))
#define AT91_MC_SMC_NWS			GENMASK(6, 0)
#define AT91_MC_SMC_NWS_(x)		((x) << 0)
#define AT91_MC_SMC_WSEN		BIT(7)
#define AT91_MC_SMC_TDF			GENMASK(11, 8)
#define AT91_MC_SMC_TDF_(x)		((x) << 8)
#define AT91_MC_SMC_TDF_MAX		0xf
#define AT91_MC_SMC_BAT			BIT(12)
#define AT91_MC_SMC_DBW			GENMASK(14, 13)
#define AT91_MC_SMC_DBW_16		(1 << 13)
#define AT91_MC_SMC_DBW_8		(2 << 13)
#define AT91_MC_SMC_DPR			BIT(15)
#define AT91_MC_SMC_ACSS		GENMASK(17, 16)
#define AT91_MC_SMC_ACSS_(x)		((x) << 16)
#define AT91_MC_SMC_ACSS_MAX		3
#define AT91_MC_SMC_RWSETUP		GENMASK(26, 24)
#define AT91_MC_SMC_RWSETUP_(x)		((x) << 24)
#define AT91_MC_SMC_RWHOLD		GENMASK(30, 28)
#define AT91_MC_SMC_RWHOLD_(x)		((x) << 28)
#define AT91_MC_SMC_RWHOLDSETUP_MAX	7

/* SDRAM Controller registers */
#define AT91_MC_SDRAMC_MR		0x90
#define AT91_MC_SDRAMC_MODE		GENMASK(3, 0)
#define AT91_MC_SDRAMC_MODE_NORMAL	(0 << 0)
#define AT91_MC_SDRAMC_MODE_NOP		(1 << 0)
#define AT91_MC_SDRAMC_MODE_PRECHARGE	(2 << 0)
#define AT91_MC_SDRAMC_MODE_LMR		(3 << 0)
#define AT91_MC_SDRAMC_MODE_REFRESH	(4 << 0)
#define AT91_MC_SDRAMC_DBW_16		BIT(4)

#define AT91_MC_SDRAMC_TR		0x94
#define AT91_MC_SDRAMC_COUNT		GENMASK(11, 0)

#define AT91_MC_SDRAMC_CR		0x98
#define AT91_MC_SDRAMC_NC		GENMASK(1, 0)
#define AT91_MC_SDRAMC_NC_8		(0 << 0)
#define AT91_MC_SDRAMC_NC_9		(1 << 0)
#define AT91_MC_SDRAMC_NC_10		(2 << 0)
#define AT91_MC_SDRAMC_NC_11		(3 << 0)
#define AT91_MC_SDRAMC_NR		GENMASK(3, 2)
#define AT91_MC_SDRAMC_NR_11		(0 << 2)
#define AT91_MC_SDRAMC_NR_12		(1 << 2)
#define AT91_MC_SDRAMC_NR_13		(2 << 2)
#define AT91_MC_SDRAMC_NB		BIT(4)
#define AT91_MC_SDRAMC_NB_2		(0 << 4)
#define AT91_MC_SDRAMC_NB_4		(1 << 4)
#define AT91_MC_SDRAMC_CAS		GENMASK(6, 5)
#define AT91_MC_SDRAMC_CAS_2		(2 << 5)
#define AT91_MC_SDRAMC_TWR		GENMASK(10,  7)
#define AT91_MC_SDRAMC_TRC		GENMASK(14, 11)
#define AT91_MC_SDRAMC_TRP		GENMASK(18, 15)
#define AT91_MC_SDRAMC_TRCD		GENMASK(22, 19)
#define AT91_MC_SDRAMC_TRAS		GENMASK(26, 23)
#define AT91_MC_SDRAMC_TXSR		GENMASK(30, 27)

#define AT91_MC_SDRAMC_SRR		0x9c
#define AT91_MC_SDRAMC_SRCB		BIT(0)

#define AT91_MC_SDRAMC_LPR		0xa0
#define AT91_MC_SDRAMC_LPCB		BIT(0)

#define AT91_MC_SDRAMC_IER		0xa4
#define AT91_MC_SDRAMC_IDR		0xa8
#define AT91_MC_SDRAMC_IMR		0xac
#define AT91_MC_SDRAMC_ISR		0xb0
#define AT91_MC_SDRAMC_RES		BIT(0)

/* Burst Flash Controller register */
#define AT91_MC_BFC_MR			0xc0
#define AT91_MC_BFC_BFCOM		GENMASK(1, 0)
#define AT91_MC_BFC_BFCOM_DISABLED	(0 << 0)
#define AT91_MC_BFC_BFCOM_ASYNC		(1 << 0)
#define AT91_MC_BFC_BFCOM_BURST		(2 << 0)
#define AT91_MC_BFC_BFCC		GENMASK(3, 2)
#define AT91_MC_BFC_BFCC_MCK		(1 << 2)
#define AT91_MC_BFC_BFCC_DIV2		(2 << 2)
#define AT91_MC_BFC_BFCC_DIV4		(3 << 2)
#define AT91_MC_BFC_AVL			GENMASK(7,  4)
#define AT91_MC_BFC_PAGES		GENMASK(10, 8)
#define AT91_MC_BFC_PAGES_NO_PAGE	(0 << 8)
#define AT91_MC_BFC_PAGES_16		(1 << 8)
#define AT91_MC_BFC_PAGES_32		(2 << 8)
#define AT91_MC_BFC_PAGES_64		(3 << 8)
#define AT91_MC_BFC_PAGES_128		(4 << 8)
#define AT91_MC_BFC_PAGES_256		(5 << 8)
#define AT91_MC_BFC_PAGES_512		(6 << 8)
#define AT91_MC_BFC_PAGES_1024		(7 << 8)
#define AT91_MC_BFC_OEL			GENMASK(13, 12)
#define AT91_MC_BFC_BAAEN		BIT(16)
#define AT91_MC_BFC_BFOEH		BIT(17)
#define AT91_MC_BFC_MUXEN		BIT(18)
#define AT91_MC_BFC_RDYEN		BIT(19)

#endif /* _LINUX_MFD_SYSCON_ATMEL_MC_H_ */
