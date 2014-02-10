/*
 *  Maxim (Dallas) MAX3107/8/9, MAX14830 serial driver
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
 *	.uart_flags[0]	= MAX310X_ECHO_SUPRESS | MAX310X_AUTO_DIR_CTRL,
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

#define MAX310X_MAX_UARTS	4

/* MAX310X platform data structure */
struct max310x_pdata {
	/* Flags global to UART port */
	const u8		uart_flags[MAX310X_MAX_UARTS];
#define MAX310X_ECHO_SUPRESS	(0x00000002)	/* Enable echo supress */
#define MAX310X_AUTO_DIR_CTRL	(0x00000004)	/* Enable Auto direction
						 * control (RS-485)
						 */
	/* GPIO base number (can be negative) */
	const int		gpio_base;
};

#endif
