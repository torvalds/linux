/*
 * Header file for the Atmel DDR/SDR SDRAM Controller
 *
 * Copyright (C) 2016 Atmel Corporation
 *
 * Author: Wenyou Yang <wenyou.yang@atmel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __AT91_SFR_H__
#define __AT91_SFR_H__

#define SFR_DDRCFG		0x04	/* DDR Configuration Register */
/* 0x08 ~ 0x0c: Reserved */
#define SFR_OHCIICR		0x10	/* OHCI Interrupt Configuration Register */
#define SFR_OHCIISR		0x14	/* OHCI Interrupt Status Register */

#define SFR_OHCIICR_SUSPEND_A	BIT(8)
#define SFR_OHCIICR_SUSPEND_B	BIT(9)
#define SFR_OHCIICR_SUSPEND_C	BIT(10)

#define SFR_OHCIICR_USB_SUSPEND	(SFR_OHCIICR_SUSPEND_A | \
				 SFR_OHCIICR_SUSPEND_B | \
				 SFR_OHCIICR_SUSPEND_C)

#endif
