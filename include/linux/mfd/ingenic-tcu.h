/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for the Ingenic JZ47xx TCU driver
 */
#ifndef __LINUX_MFD_INGENIC_TCU_H_
#define __LINUX_MFD_INGENIC_TCU_H_

#include <linux/bitops.h>

#define TCU_REG_WDT_TDR		0x00
#define TCU_REG_WDT_TCER	0x04
#define TCU_REG_WDT_TCNT	0x08
#define TCU_REG_WDT_TCSR	0x0c
#define TCU_REG_TER		0x10
#define TCU_REG_TESR		0x14
#define TCU_REG_TECR		0x18
#define TCU_REG_TSR		0x1c
#define TCU_REG_TFR		0x20
#define TCU_REG_TFSR		0x24
#define TCU_REG_TFCR		0x28
#define TCU_REG_TSSR		0x2c
#define TCU_REG_TMR		0x30
#define TCU_REG_TMSR		0x34
#define TCU_REG_TMCR		0x38
#define TCU_REG_TSCR		0x3c
#define TCU_REG_TDFR0		0x40
#define TCU_REG_TDHR0		0x44
#define TCU_REG_TCNT0		0x48
#define TCU_REG_TCSR0		0x4c
#define TCU_REG_OST_DR		0xe0
#define TCU_REG_OST_CNTL	0xe4
#define TCU_REG_OST_CNTH	0xe8
#define TCU_REG_OST_TCSR	0xec
#define TCU_REG_TSTR		0xf0
#define TCU_REG_TSTSR		0xf4
#define TCU_REG_TSTCR		0xf8
#define TCU_REG_OST_CNTHBUF	0xfc

#define TCU_TCSR_RESERVED_BITS		0x3f
#define TCU_TCSR_PARENT_CLOCK_MASK	0x07
#define TCU_TCSR_PRESCALE_LSB		3
#define TCU_TCSR_PRESCALE_MASK		0x38

#define TCU_TCSR_PWM_SD		BIT(9)	/* 0: Shutdown abruptly 1: gracefully */
#define TCU_TCSR_PWM_INITL_HIGH	BIT(8)	/* Sets the initial output level */
#define TCU_TCSR_PWM_EN		BIT(7)	/* PWM pin output enable */

#define TCU_WDT_TCER_TCEN	BIT(0)	/* Watchdog timer enable */

#define TCU_CHANNEL_STRIDE	0x10
#define TCU_REG_TDFRc(c)	(TCU_REG_TDFR0 + ((c) * TCU_CHANNEL_STRIDE))
#define TCU_REG_TDHRc(c)	(TCU_REG_TDHR0 + ((c) * TCU_CHANNEL_STRIDE))
#define TCU_REG_TCNTc(c)	(TCU_REG_TCNT0 + ((c) * TCU_CHANNEL_STRIDE))
#define TCU_REG_TCSRc(c)	(TCU_REG_TCSR0 + ((c) * TCU_CHANNEL_STRIDE))

#endif /* __LINUX_MFD_INGENIC_TCU_H_ */
