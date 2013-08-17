/*
 *
 *  Copyright (C) 2007 Christian Pellegrin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifndef _LINUX_SERIAL_MAX3100_H
#define _LINUX_SERIAL_MAX3100_H 1


/**
 * struct plat_max3100 - MAX3100 SPI UART platform data
 * @loopback:            force MAX3100 in loopback
 * @crystal:             1 for 3.6864 Mhz, 0 for 1.8432
 * @max3100_hw_suspend:  MAX3100 has a shutdown pin. This is a hook
 *                       called on suspend and resume to activate it.
 * @poll_time:           poll time for CTS signal in ms, 0 disables (so no hw
 *                       flow ctrl is possible but you have less CPU usage)
 *
 * You should use this structure in your machine description to specify
 * how the MAX3100 is connected. Example:
 *
 * static struct plat_max3100 max3100_plat_data = {
 *  .loopback = 0,
 *  .crystal = 0,
 *  .poll_time = 100,
 * };
 *
 * static struct spi_board_info spi_board_info[] = {
 * {
 *  .modalias	= "max3100",
 *  .platform_data	= &max3100_plat_data,
 *  .irq		= IRQ_EINT12,
 *  .max_speed_hz	= 5*1000*1000,
 *  .chip_select	= 0,
 * },
 * };
 *
 **/
struct plat_max3100 {
	int loopback;
	int crystal;
	void (*max3100_hw_suspend) (int suspend);
	int poll_time;
};

#endif
