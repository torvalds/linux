/*
 *  NXP (Philips) SCC+++(SCN+++) serial driver
 *
 *  Copyright (C) 2012 Alexander Shiyan <shc_work@mail.ru>
 *
 *  Based on sc26xx.c, by Thomas Bogendörfer (tsbogend@alpha.franken.de)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef __SCCNXP_H
#define __SCCNXP_H

#define SCCNXP_MAX_UARTS	2

/* Output lines */
#define LINE_OP0		1
#define LINE_OP1		2
#define LINE_OP2		3
#define LINE_OP3		4
#define LINE_OP4		5
#define LINE_OP5		6
#define LINE_OP6		7
#define LINE_OP7		8

/* Input lines */
#define LINE_IP0		9
#define LINE_IP1		10
#define LINE_IP2		11
#define LINE_IP3		12
#define LINE_IP4		13
#define LINE_IP5		14
#define LINE_IP6		15

/* Signals */
#define DTR_OP			0	/* DTR */
#define RTS_OP			4	/* RTS */
#define DSR_IP			8	/* DSR */
#define CTS_IP			12	/* CTS */
#define DCD_IP			16	/* DCD */
#define RNG_IP			20	/* RNG */

#define DIR_OP			24	/* Special signal for control RS-485.
					 * Goes high when transmit,
					 * then goes low.
					 */

/* Routing control signal 'sig' to line 'line' */
#define MCTRL_SIG(sig, line)	((line) << (sig))

/*
 * Example board initialization data:
 *
 * static struct resource sc2892_resources[] = {
 *	DEFINE_RES_MEM(UART_PHYS_START, 0x10),
 *	DEFINE_RES_IRQ(IRQ_EXT2),
 * };
 *
 * static struct sccnxp_pdata sc2892_info = {
 *	.frequency	= 3686400,
 *	.mctrl_cfg[0]	= MCTRL_SIG(DIR_OP, LINE_OP0),
 *	.mctrl_cfg[1]	= MCTRL_SIG(DIR_OP, LINE_OP1),
 * };
 *
 * static struct platform_device sc2892 = {
 *	.name		= "sc2892",
 *	.id		= -1,
 *	.resource	= sc2892_resources,
 *	.num_resources	= ARRAY_SIZE(sc2892_resources),
 *	.dev = {
 *		.platform_data	= &sc2892_info,
 *	},
 * };
 */

/* SCCNXP platform data structure */
struct sccnxp_pdata {
	/* Frequency (extrenal clock or crystal) */
	int			frequency;
	/* Shift for A0 line */
	const u8		reg_shift;
	/* Modem control lines configuration */
	const u32		mctrl_cfg[SCCNXP_MAX_UARTS];
	/* Called during startup */
	void (*init)(void);
	/* Called before finish */
	void (*exit)(void);
};

#endif
