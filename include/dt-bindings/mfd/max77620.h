/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides macros for MAXIM MAX77620 device bindings.
 *
 * Copyright (c) 2016, NVIDIA Corporation.
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 */

#ifndef _DT_BINDINGS_MFD_MAX77620_H
#define _DT_BINDINGS_MFD_MAX77620_H

/* MAX77620 interrupts */
#define MAX77620_IRQ_TOP_GLBL		0 /* Low-Battery */
#define MAX77620_IRQ_TOP_SD		1 /* SD power fail */
#define MAX77620_IRQ_TOP_LDO		2 /* LDO power fail */
#define MAX77620_IRQ_TOP_GPIO		3 /* GPIO internal int to MAX77620 */
#define MAX77620_IRQ_TOP_RTC		4 /* RTC */
#define MAX77620_IRQ_TOP_32K		5 /* 32kHz oscillator */
#define MAX77620_IRQ_TOP_ONOFF		6 /* ON/OFF oscillator */
#define MAX77620_IRQ_LBT_MBATLOW	7 /* Thermal alarm status, > 120C */
#define MAX77620_IRQ_LBT_TJALRM1	8 /* Thermal alarm status, > 120C */
#define MAX77620_IRQ_LBT_TJALRM2	9 /* Thermal alarm status, > 140C */

/* FPS event source */
#define MAX77620_FPS_EVENT_SRC_EN0		0
#define MAX77620_FPS_EVENT_SRC_EN1		1
#define MAX77620_FPS_EVENT_SRC_SW		2

/* Device state when FPS event LOW  */
#define MAX77620_FPS_INACTIVE_STATE_SLEEP	0
#define MAX77620_FPS_INACTIVE_STATE_LOW_POWER	1

/* FPS source */
#define MAX77620_FPS_SRC_0			0
#define MAX77620_FPS_SRC_1			1
#define MAX77620_FPS_SRC_2			2
#define MAX77620_FPS_SRC_NONE			3
#define MAX77620_FPS_SRC_DEF			4

#endif
