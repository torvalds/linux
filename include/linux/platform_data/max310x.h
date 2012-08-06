/*
 *  Maxim (Dallas) MAX3107/8 serial driver
 *
 *  Copyright (C) 2012 Alexander Shiyan <shc_work@mail.ru>
 *
 *  Based on max3100.c, by Christian Pellegrin <chripell@evolware.org>
 *  Based on max3110.c, by Feng Tang <feng.tang@intel.com>
 *  Based on max3107.c, by Aavamobile
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef _MAX310X_H_
#define _MAX310X_H_

/*
 * Example board initialization data:
 *
 * static struct max310x_pdata max3107_pdata = {
 *	.driver_flags	= MAX310X_EXT_CLK,
 *	.uart_flags[0]	= MAX310X_ECHO_SUPRESS | MAX310X_AUTO_DIR_CTRL,
 *	.frequency	= 3686400,
 *	.gpio_base	= -1,
 * };
 *
 * static struct spi_board_info spi_device_max3107[] = {
 *	{
 *		.modalias	= "max3107",
 *		.irq		= IRQ_EINT3,
 *		.bus_num	= 1,
 *		.chip_select	= 1,
 *		.platform_data	= &max3107_pdata,
 *	},
 * };
 */

#define MAX310X_MAX_UARTS	1

/* MAX310X platform data structure */
struct max310x_pdata {
	/* Flags global to driver */
	const u8		driver_flags:2;
#define MAX310X_EXT_CLK		(0x00000001)	/* External clock enable */
#define MAX310X_AUTOSLEEP	(0x00000002)	/* Enable AutoSleep mode */
	/* Flags global to UART port */
	const u8		uart_flags[MAX310X_MAX_UARTS];
#define MAX310X_LOOPBACK	(0x00000001)	/* Loopback mode enable */
#define MAX310X_ECHO_SUPRESS	(0x00000002)	/* Enable echo supress */
#define MAX310X_AUTO_DIR_CTRL	(0x00000004)	/* Enable Auto direction
						 * control (RS-485)
						 */
	/* Frequency (extrenal clock or crystal) */
	const int		frequency;
	/* GPIO base number (can be negative) */
	const int		gpio_base;
	/* Called during startup */
	void (*init)(void);
	/* Called before finish */
	void (*exit)(void);
	/* Suspend callback */
	void (*suspend)(int do_suspend);
};

#endif
