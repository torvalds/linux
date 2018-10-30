/*
 * rt5514-spi.h  --  RT5514 driver
 *
 * Copyright 2015 Realtek Semiconductor Corp.
 * Author: Oder Chiou <oder_chiou@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT5514_SPI_H__
#define __RT5514_SPI_H__

/**
 * RT5514_SPI_BUF_LEN is the buffer size of SPI master controller.
*/
#define RT5514_SPI_BUF_LEN		240

#define RT5514_BUFFER_VOICE_BASE	0x18000200
#define RT5514_BUFFER_VOICE_LIMIT	0x18000204
#define RT5514_BUFFER_VOICE_WP		0x1800020c
#define RT5514_IRQ_CTRL			0x18002094

#define RT5514_IRQ_STATUS_BIT		(0x1 << 5)

/* SPI Command */
enum {
	RT5514_SPI_CMD_16_READ = 0,
	RT5514_SPI_CMD_16_WRITE,
	RT5514_SPI_CMD_32_READ,
	RT5514_SPI_CMD_32_WRITE,
	RT5514_SPI_CMD_BURST_READ,
	RT5514_SPI_CMD_BURST_WRITE,
};

int rt5514_spi_burst_read(unsigned int addr, u8 *rxbuf, size_t len);
int rt5514_spi_burst_write(u32 addr, const u8 *txbuf, size_t len);

#endif /* __RT5514_SPI_H__ */
