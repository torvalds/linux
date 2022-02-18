/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2021 Microchip Inc.
 *
 * Author: Kavyasree Kotagiri <kavyasree.kotagiri@microchip.com>
 */

#ifndef _DT_BINDINGS_CLK_LAN966X_H
#define _DT_BINDINGS_CLK_LAN966X_H

#define GCK_ID_QSPI0		0
#define GCK_ID_QSPI1		1
#define GCK_ID_QSPI2		2
#define GCK_ID_SDMMC0		3
#define GCK_ID_PI		4
#define GCK_ID_MCAN0		5
#define GCK_ID_MCAN1		6
#define GCK_ID_FLEXCOM0		7
#define GCK_ID_FLEXCOM1		8
#define GCK_ID_FLEXCOM2		9
#define GCK_ID_FLEXCOM3		10
#define GCK_ID_FLEXCOM4		11
#define GCK_ID_TIMER		12
#define GCK_ID_USB_REFCLK	13

/* Gate clocks */
#define GCK_GATE_UHPHS		14
#define GCK_GATE_UDPHS		15
#define GCK_GATE_MCRAMC		16
#define GCK_GATE_HMATRIX	17

#define N_CLOCKS		18

#endif
