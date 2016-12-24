/*
 * Atmel SFR (Special Function Registers) register offsets and bit definitions.
 *
 * Copyright (C) 2016 Atmel
 *
 * Author: Ludovic Desroches <ludovic.desroches@atmel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_MFD_SYSCON_ATMEL_SFR_H
#define _LINUX_MFD_SYSCON_ATMEL_SFR_H

#define AT91_SFR_DDRCFG		0x04	/* DDR Configuration Register */
/* 0x08 ~ 0x0c: Reserved */
#define AT91_SFR_OHCIICR	0x10	/* OHCI INT Configuration Register */
#define AT91_SFR_OHCIISR	0x14	/* OHCI INT Status Register */
#define AT91_SFR_I2SCLKSEL	0x90	/* I2SC Register */

/* Field definitions */
#define AT91_OHCIICR_SUSPEND_A	BIT(8)
#define AT91_OHCIICR_SUSPEND_B	BIT(9)
#define AT91_OHCIICR_SUSPEND_C	BIT(10)

#define AT91_OHCIICR_USB_SUSPEND	(AT91_OHCIICR_SUSPEND_A | \
					 AT91_OHCIICR_SUSPEND_B | \
					 AT91_OHCIICR_SUSPEND_C)


#endif /* _LINUX_MFD_SYSCON_ATMEL_SFR_H */
