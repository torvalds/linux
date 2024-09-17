/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Broadcom Corporation
 * Copyright 2013 Linaro Limited
 */

#ifndef _CLOCK_BCM281XX_H
#define _CLOCK_BCM281XX_H

/*
 * This file defines the values used to specify clocks provided by
 * the clock control units (CCUs) on Broadcom BCM281XX family SoCs.
 */

/*
 * These are the bcm281xx CCU device tree "compatible" strings.
 * We're stuck with using "bcm11351" in the string because wild
 * cards aren't allowed, and that name was the first one defined
 * in this family of devices.
 */
#define BCM281XX_DT_ROOT_CCU_COMPAT	"brcm,bcm11351-root-ccu"
#define BCM281XX_DT_AON_CCU_COMPAT	"brcm,bcm11351-aon-ccu"
#define BCM281XX_DT_HUB_CCU_COMPAT	"brcm,bcm11351-hub-ccu"
#define BCM281XX_DT_MASTER_CCU_COMPAT	"brcm,bcm11351-master-ccu"
#define BCM281XX_DT_SLAVE_CCU_COMPAT	"brcm,bcm11351-slave-ccu"

/* root CCU clock ids */

#define BCM281XX_ROOT_CCU_FRAC_1M		0
#define BCM281XX_ROOT_CCU_CLOCK_COUNT		1

/* aon CCU clock ids */

#define BCM281XX_AON_CCU_HUB_TIMER		0
#define BCM281XX_AON_CCU_PMU_BSC		1
#define BCM281XX_AON_CCU_PMU_BSC_VAR		2
#define BCM281XX_AON_CCU_CLOCK_COUNT		3

/* hub CCU clock ids */

#define BCM281XX_HUB_CCU_TMON_1M		0
#define BCM281XX_HUB_CCU_CLOCK_COUNT		1

/* master CCU clock ids */

#define BCM281XX_MASTER_CCU_SDIO1		0
#define BCM281XX_MASTER_CCU_SDIO2		1
#define BCM281XX_MASTER_CCU_SDIO3		2
#define BCM281XX_MASTER_CCU_SDIO4		3
#define BCM281XX_MASTER_CCU_USB_IC		4
#define BCM281XX_MASTER_CCU_HSIC2_48M		5
#define BCM281XX_MASTER_CCU_HSIC2_12M		6
#define BCM281XX_MASTER_CCU_CLOCK_COUNT		7

/* slave CCU clock ids */

#define BCM281XX_SLAVE_CCU_UARTB		0
#define BCM281XX_SLAVE_CCU_UARTB2		1
#define BCM281XX_SLAVE_CCU_UARTB3		2
#define BCM281XX_SLAVE_CCU_UARTB4		3
#define BCM281XX_SLAVE_CCU_SSP0			4
#define BCM281XX_SLAVE_CCU_SSP2			5
#define BCM281XX_SLAVE_CCU_BSC1			6
#define BCM281XX_SLAVE_CCU_BSC2			7
#define BCM281XX_SLAVE_CCU_BSC3			8
#define BCM281XX_SLAVE_CCU_PWM			9
#define BCM281XX_SLAVE_CCU_CLOCK_COUNT		10

#endif /* _CLOCK_BCM281XX_H */
