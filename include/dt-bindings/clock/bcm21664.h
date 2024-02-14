/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Broadcom Corporation
 * Copyright 2013 Linaro Limited
 */

#ifndef _CLOCK_BCM21664_H
#define _CLOCK_BCM21664_H

/*
 * This file defines the values used to specify clocks provided by
 * the clock control units (CCUs) on Broadcom BCM21664 family SoCs.
 */

/* bcm21664 CCU device tree "compatible" strings */
#define BCM21664_DT_ROOT_CCU_COMPAT	"brcm,bcm21664-root-ccu"
#define BCM21664_DT_AON_CCU_COMPAT	"brcm,bcm21664-aon-ccu"
#define BCM21664_DT_MASTER_CCU_COMPAT	"brcm,bcm21664-master-ccu"
#define BCM21664_DT_SLAVE_CCU_COMPAT	"brcm,bcm21664-slave-ccu"

/* root CCU clock ids */

#define BCM21664_ROOT_CCU_FRAC_1M		0
#define BCM21664_ROOT_CCU_CLOCK_COUNT		1

/* aon CCU clock ids */

#define BCM21664_AON_CCU_HUB_TIMER		0
#define BCM21664_AON_CCU_CLOCK_COUNT		1

/* master CCU clock ids */

#define BCM21664_MASTER_CCU_SDIO1		0
#define BCM21664_MASTER_CCU_SDIO2		1
#define BCM21664_MASTER_CCU_SDIO3		2
#define BCM21664_MASTER_CCU_SDIO4		3
#define BCM21664_MASTER_CCU_SDIO1_SLEEP		4
#define BCM21664_MASTER_CCU_SDIO2_SLEEP		5
#define BCM21664_MASTER_CCU_SDIO3_SLEEP		6
#define BCM21664_MASTER_CCU_SDIO4_SLEEP		7
#define BCM21664_MASTER_CCU_CLOCK_COUNT		8

/* slave CCU clock ids */

#define BCM21664_SLAVE_CCU_UARTB		0
#define BCM21664_SLAVE_CCU_UARTB2		1
#define BCM21664_SLAVE_CCU_UARTB3		2
#define BCM21664_SLAVE_CCU_BSC1			3
#define BCM21664_SLAVE_CCU_BSC2			4
#define BCM21664_SLAVE_CCU_BSC3			5
#define BCM21664_SLAVE_CCU_BSC4			6
#define BCM21664_SLAVE_CCU_CLOCK_COUNT		7

#endif /* _CLOCK_BCM21664_H */
