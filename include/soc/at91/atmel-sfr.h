/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Atmel SFR (Special Function Registers) register offsets and bit definitions.
 *
 * Copyright (C) 2016 Atmel
 *
 * Author: Ludovic Desroches <ludovic.desroches@atmel.com>
 */

#ifndef _LINUX_MFD_SYSCON_ATMEL_SFR_H
#define _LINUX_MFD_SYSCON_ATMEL_SFR_H

#define AT91_SFR_DDRCFG		0x04	/* DDR Configuration Register */
#define AT91_SFR_CCFG_EBICSA	0x04	/* EBI Chip Select Register */
/* 0x08 ~ 0x0c: Reserved */
#define AT91_SFR_OHCIICR	0x10	/* OHCI INT Configuration Register */
#define AT91_SFR_OHCIISR	0x14	/* OHCI INT Status Register */
#define AT91_SFR_UTMICKTRIM	0x30	/* UTMI Clock Trimming Register */
#define AT91_SFR_UTMISWAP	0x3c	/* UTMI DP/DM Pin Swapping Register */
#define AT91_SFR_LS		0x7c	/* Light Sleep Register */
#define AT91_SFR_I2SCLKSEL	0x90	/* I2SC Register */
#define AT91_SFR_WPMR		0xe4	/* Write Protection Mode Register */

/* Field definitions */
#define AT91_SFR_CCFG_EBI_CSA(cs, val)		((val) << (cs))
#define AT91_SFR_CCFG_EBI_DBPUC			BIT(8)
#define AT91_SFR_CCFG_EBI_DBPDC			BIT(9)
#define AT91_SFR_CCFG_EBI_DRIVE			BIT(17)
#define AT91_SFR_CCFG_NFD0_ON_D16		BIT(24)
#define AT91_SFR_CCFG_DDR_MP_EN			BIT(25)

#define AT91_SFR_OHCIICR_RES(x)			BIT(x)
#define AT91_SFR_OHCIICR_ARIE			BIT(4)
#define AT91_SFR_OHCIICR_APPSTART		BIT(5)
#define AT91_SFR_OHCIICR_USB_SUSP(x)		BIT(8 + (x))
#define AT91_SFR_OHCIICR_UDPPUDIS		BIT(23)
#define AT91_OHCIICR_USB_SUSPEND		GENMASK(10, 8)

#define AT91_SFR_OHCIISR_RIS(x)			BIT(x)

#define AT91_UTMICKTRIM_FREQ			GENMASK(1, 0)

#define AT91_SFR_UTMISWAP_PORT(x)		BIT(x)

#define AT91_SFR_LS_VALUE(x)			BIT(x)
#define AT91_SFR_LS_MEM_POWER_GATING_ULP1_EN	BIT(16)

#define AT91_SFR_WPMR_WPEN			BIT(0)
#define AT91_SFR_WPMR_WPKEY_MASK		GENMASK(31, 8)

#endif /* _LINUX_MFD_SYSCON_ATMEL_SFR_H */
