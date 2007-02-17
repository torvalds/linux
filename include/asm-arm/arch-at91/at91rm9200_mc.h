/*
 * include/asm-arm/arch-at91/at91rm9200_mc.h
 *
 * Copyright (C) 2005 Ivan Kokshaysky
 * Copyright (C) SAN People
 *
 * Memory Controllers (MC, EBI, SMC, SDRAMC, BFC) - System peripherals registers.
 * Based on AT91RM9200 datasheet revision E.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91RM9200_MC_H
#define AT91RM9200_MC_H

/* Memory Controller */
#define AT91_MC_RCR		(AT91_MC + 0x00)	/* MC Remap Control Register */
#define		AT91_MC_RCB		(1 <<  0)		/* Remap Command Bit */

#define AT91_MC_ASR		(AT91_MC + 0x04)	/* MC Abort Status Register */
#define		AT91_MC_UNADD		(1 <<  0)		/* Undefined Address Abort Status */
#define		AT91_MC_MISADD		(1 <<  1)		/* Misaligned Address Abort Status */
#define		AT91_MC_ABTSZ		(3 <<  8)		/* Abort Size Status */
#define			AT91_MC_ABTSZ_BYTE		(0 << 8)
#define			AT91_MC_ABTSZ_HALFWORD		(1 << 8)
#define			AT91_MC_ABTSZ_WORD		(2 << 8)
#define		AT91_MC_ABTTYP		(3 << 10)		/* Abort Type Status */
#define			AT91_MC_ABTTYP_DATAREAD		(0 << 10)
#define			AT91_MC_ABTTYP_DATAWRITE	(1 << 10)
#define			AT91_MC_ABTTYP_FETCH		(2 << 10)
#define		AT91_MC_MST0		(1 << 16)		/* ARM920T Abort Source */
#define		AT91_MC_MST1		(1 << 17)		/* PDC Abort Source */
#define		AT91_MC_MST2		(1 << 18)		/* UHP Abort Source */
#define		AT91_MC_MST3		(1 << 19)		/* EMAC Abort Source */
#define		AT91_MC_SVMST0		(1 << 24)		/* Saved ARM920T Abort Source */
#define		AT91_MC_SVMST1		(1 << 25)		/* Saved PDC Abort Source */
#define		AT91_MC_SVMST2		(1 << 26)		/* Saved UHP Abort Source */
#define		AT91_MC_SVMST3		(1 << 27)		/* Saved EMAC Abort Source */

#define AT91_MC_AASR		(AT91_MC + 0x08)	/* MC Abort Address Status Register */

#define AT91_MC_MPR		(AT91_MC + 0x0c)	/* MC Master Priority Register */
#define		AT91_MPR_MSTP0		(7 <<  0)		/* ARM920T Priority */
#define		AT91_MPR_MSTP1		(7 <<  4)		/* PDC Priority */
#define		AT91_MPR_MSTP2		(7 <<  8)		/* UHP Priority */
#define		AT91_MPR_MSTP3		(7 << 12)		/* EMAC Priority */

/* External Bus Interface (EBI) registers */
#define AT91_EBI_CSA		(AT91_MC + 0x60)	/* Chip Select Assignment Register */
#define		AT91_EBI_CS0A		(1 << 0)		/* Chip Select 0 Assignment */
#define			AT91_EBI_CS0A_SMC		(0 << 0)
#define			AT91_EBI_CS0A_BFC		(1 << 0)
#define		AT91_EBI_CS1A		(1 << 1)		/* Chip Select 1 Assignment */
#define			AT91_EBI_CS1A_SMC		(0 << 1)
#define			AT91_EBI_CS1A_SDRAMC		(1 << 1)
#define		AT91_EBI_CS3A		(1 << 3)		/* Chip Select 2 Assignment */
#define			AT91_EBI_CS3A_SMC		(0 << 3)
#define			AT91_EBI_CS3A_SMC_SMARTMEDIA	(1 << 3)
#define		AT91_EBI_CS4A		(1 << 4)		/* Chip Select 3 Assignment */
#define			AT91_EBI_CS4A_SMC		(0 << 4)
#define			AT91_EBI_CS4A_SMC_COMPACTFLASH	(1 << 4)
#define AT91_EBI_CFGR		(AT91_MC + 0x64)	/* Configuration Register */
#define		AT91_EBI_DBPUC		(1 << 0)		/* Data Bus Pull-Up Configuration */

/* Static Memory Controller (SMC) registers */
#define	AT91_SMC_CSR(n)		(AT91_MC + 0x70 + ((n) * 4))/* SMC Chip Select Register */
#define		AT91_SMC_NWS		(0x7f <<  0)		/* Number of Wait States */
#define			AT91_SMC_NWS_(x)	((x) << 0)
#define		AT91_SMC_WSEN		(1    <<  7)		/* Wait State Enable */
#define		AT91_SMC_TDF		(0xf  <<  8)		/* Data Float Time */
#define			AT91_SMC_TDF_(x)	((x) << 8)
#define		AT91_SMC_BAT		(1    << 12)		/* Byte Access Type */
#define		AT91_SMC_DBW		(3    << 13)		/* Data Bus Width */
#define			AT91_SMC_DBW_16		(1 << 13)
#define			AT91_SMC_DBW_8		(2 << 13)
#define		AT91_SMC_DPR		(1 << 15)		/* Data Read Protocol */
#define		AT91_SMC_ACSS		(3 << 16)		/* Address to Chip Select Setup */
#define			AT91_SMC_ACSS_STD	(0 << 16)
#define			AT91_SMC_ACSS_1		(1 << 16)
#define			AT91_SMC_ACSS_2		(2 << 16)
#define			AT91_SMC_ACSS_3		(3 << 16)
#define		AT91_SMC_RWSETUP	(7 << 24)		/* Read & Write Signal Time Setup */
#define			AT91_SMC_RWSETUP_(x)	((x) << 24)
#define		AT91_SMC_RWHOLD		(7 << 28)		/* Read & Write Signal Hold Time */
#define			AT91_SMC_RWHOLD_(x)	((x) << 28)

/* SDRAM Controller registers */
#define AT91_SDRAMC_MR		(AT91_MC + 0x90)	/* Mode Register */
#define		AT91_SDRAMC_MODE	(0xf << 0)		/* Command Mode */
#define			AT91_SDRAMC_MODE_NORMAL		(0 << 0)
#define			AT91_SDRAMC_MODE_NOP		(1 << 0)
#define			AT91_SDRAMC_MODE_PRECHARGE	(2 << 0)
#define			AT91_SDRAMC_MODE_LMR		(3 << 0)
#define			AT91_SDRAMC_MODE_REFRESH	(4 << 0)
#define		AT91_SDRAMC_DBW		(1   << 4)		/* Data Bus Width */
#define			AT91_SDRAMC_DBW_32	(0 << 4)
#define			AT91_SDRAMC_DBW_16	(1 << 4)

#define AT91_SDRAMC_TR		(AT91_MC + 0x94)	/* Refresh Timer Register */
#define		AT91_SDRAMC_COUNT	(0xfff << 0)		/* Refresh Timer Count */

#define AT91_SDRAMC_CR		(AT91_MC + 0x98)	/* Configuration Register */
#define		AT91_SDRAMC_NC		(3   <<  0)		/* Number of Column Bits */
#define			AT91_SDRAMC_NC_8	(0 << 0)
#define			AT91_SDRAMC_NC_9	(1 << 0)
#define			AT91_SDRAMC_NC_10	(2 << 0)
#define			AT91_SDRAMC_NC_11	(3 << 0)
#define		AT91_SDRAMC_NR		(3   <<  2)		/* Number of Row Bits */
#define			AT91_SDRAMC_NR_11	(0 << 2)
#define			AT91_SDRAMC_NR_12	(1 << 2)
#define			AT91_SDRAMC_NR_13	(2 << 2)
#define		AT91_SDRAMC_NB		(1   <<  4)		/* Number of Banks */
#define			AT91_SDRAMC_NB_2	(0 << 4)
#define			AT91_SDRAMC_NB_4	(1 << 4)
#define		AT91_SDRAMC_CAS		(3   <<  5)		/* CAS Latency */
#define			AT91_SDRAMC_CAS_2	(2 << 5)
#define		AT91_SDRAMC_TWR		(0xf <<  7)		/* Write Recovery Delay */
#define		AT91_SDRAMC_TRC		(0xf << 11)		/* Row Cycle Delay */
#define		AT91_SDRAMC_TRP		(0xf << 15)		/* Row Precharge Delay */
#define		AT91_SDRAMC_TRCD	(0xf << 19)		/* Row to Column Delay */
#define		AT91_SDRAMC_TRAS	(0xf << 23)		/* Active to Precharge Delay */
#define		AT91_SDRAMC_TXSR	(0xf << 27)		/* Exit Self Refresh to Active Delay */

#define AT91_SDRAMC_SRR		(AT91_MC + 0x9c)	/* Self Refresh Register */
#define AT91_SDRAMC_LPR		(AT91_MC + 0xa0)	/* Low Power Register */
#define AT91_SDRAMC_IER		(AT91_MC + 0xa4)	/* Interrupt Enable Register */
#define AT91_SDRAMC_IDR		(AT91_MC + 0xa8)	/* Interrupt Disable Register */
#define AT91_SDRAMC_IMR		(AT91_MC + 0xac)	/* Interrupt Mask Register */
#define AT91_SDRAMC_ISR		(AT91_MC + 0xb0)	/* Interrupt Status Register */

/* Burst Flash Controller register */
#define AT91_BFC_MR		(AT91_MC + 0xc0)	/* Mode Register */
#define		AT91_BFC_BFCOM		(3   <<  0)		/* Burst Flash Controller Operating Mode */
#define			AT91_BFC_BFCOM_DISABLED	(0 << 0)
#define			AT91_BFC_BFCOM_ASYNC	(1 << 0)
#define			AT91_BFC_BFCOM_BURST	(2 << 0)
#define		AT91_BFC_BFCC		(3   <<  2)		/* Burst Flash Controller Clock */
#define			AT91_BFC_BFCC_MCK	(1 << 2)
#define			AT91_BFC_BFCC_DIV2	(2 << 2)
#define			AT91_BFC_BFCC_DIV4	(3 << 2)
#define		AT91_BFC_AVL		(0xf <<  4)		/* Address Valid Latency */
#define		AT91_BFC_PAGES		(7   <<  8)		/* Page Size */
#define			AT91_BFC_PAGES_NO_PAGE	(0 << 8)
#define			AT91_BFC_PAGES_16	(1 << 8)
#define			AT91_BFC_PAGES_32	(2 << 8)
#define			AT91_BFC_PAGES_64	(3 << 8)
#define			AT91_BFC_PAGES_128	(4 << 8)
#define			AT91_BFC_PAGES_256	(5 << 8)
#define			AT91_BFC_PAGES_512	(6 << 8)
#define			AT91_BFC_PAGES_1024	(7 << 8)
#define		AT91_BFC_OEL		(3   << 12)		/* Output Enable Latency */
#define		AT91_BFC_BAAEN		(1   << 16)		/* Burst Address Advance Enable */
#define		AT91_BFC_BFOEH		(1   << 17)		/* Burst Flash Output Enable Handling */
#define		AT91_BFC_MUXEN		(1   << 18)		/* Multiplexed Bus Enable */
#define		AT91_BFC_RDYEN		(1   << 19)		/* Ready Enable Mode */

#endif
