/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Maxim MAX77714 Register and data structures definition.
 *
 * Copyright (C) 2022 Luca Ceresoli
 * Author: Luca Ceresoli <luca.ceresoli@bootlin.com>
 */

#ifndef __LINUX_MFD_MAX77714_H_
#define __LINUX_MFD_MAX77714_H_

#include <linux/bits.h>

#define MAX77714_INT_TOP	0x00
#define MAX77714_INT_TOPM	0x07 /* Datasheet says "read only", but it is RW */

#define MAX77714_INT_TOP_ONOFF		BIT(1)
#define MAX77714_INT_TOP_RTC		BIT(3)
#define MAX77714_INT_TOP_GPIO		BIT(4)
#define MAX77714_INT_TOP_LDO		BIT(5)
#define MAX77714_INT_TOP_SD		BIT(6)
#define MAX77714_INT_TOP_GLBL		BIT(7)

#define MAX77714_32K_STATUS	0x30
#define MAX77714_32K_STATUS_SIOSCOK	BIT(5)
#define MAX77714_32K_STATUS_XOSCOK	BIT(4)
#define MAX77714_32K_STATUS_32KSOURCE	BIT(3)
#define MAX77714_32K_STATUS_32KLOAD_MSK	0x3
#define MAX77714_32K_STATUS_32KLOAD_SHF	1
#define MAX77714_32K_STATUS_CRYSTAL_CFG	BIT(0)

#define MAX77714_32K_CONFIG	0x31
#define MAX77714_32K_CONFIG_XOSC_RETRY	BIT(4)

#define MAX77714_CNFG_GLBL2	0x91
#define MAX77714_WDTEN			BIT(2)
#define MAX77714_WDTSLPC		BIT(3)
#define MAX77714_TWD_MASK		0x3
#define MAX77714_TWD_2s			0x0
#define MAX77714_TWD_16s		0x1
#define MAX77714_TWD_64s		0x2
#define MAX77714_TWD_128s		0x3

#define MAX77714_CNFG_GLBL3	0x92
#define MAX77714_WDTC			BIT(0)

#define MAX77714_CNFG2_ONOFF	0x94
#define MAX77714_WD_RST_WK		BIT(5)

/* Interrupts */
enum {
	MAX77714_IRQ_TOP_ONOFF,
	MAX77714_IRQ_TOP_RTC,		/* Real-time clock */
	MAX77714_IRQ_TOP_GPIO,		/* GPIOs */
	MAX77714_IRQ_TOP_LDO,		/* Low-dropout regulators */
	MAX77714_IRQ_TOP_SD,		/* Step-down regulators */
	MAX77714_IRQ_TOP_GLBL,		/* "Global resources": Low-Battery, overtemp... */
};

#endif /* __LINUX_MFD_MAX77714_H_ */
