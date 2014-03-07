/* include/linux/platform_data/spi-rockchip.h
 *
 * Copyright (C) 2014 Rockchip Electronics Ltd.
 *	luowei <lw@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ROCKCHIP_PLAT_SPI_H
#define __ROCKCHIP_PLAT_SPI_H

#include <linux/dmaengine.h>

struct platform_device;

/**
 * struct rockchip_spi_csinfo - ChipSelect description
 * @fb_delay: Slave specific feedback delay.
 *            Refer to FB_CLK_SEL register definition in SPI chapter.
 * @line: Custom 'identity' of the CS line.
 *
 * This is per SPI-Slave Chipselect information.
 * Allocate and initialize one in machine init code and make the
 * spi_board_info.controller_data point to it.
 */
struct rockchip_spi_csinfo {
	u8 fb_delay;
	unsigned line;
};

/**
 * struct rockchip_spi_info - SPI Controller defining structure
 * @src_clk_nr: Clock source index for the CLK_CFG[SPI_CLKSEL] field.
 * @num_cs: Number of CS this controller emulates.
 * @cfg_gpio: Configure pins for this SPI controller.
 */
struct rockchip_spi_info {
	int src_clk_nr;
	int spi_freq;
	int num_cs;
	int bus_num;
	int (*cfg_gpio)(void);
	dma_filter_fn filter;

	u8 transfer_mode;/*full or half duplex*/
	u8 poll_mode;	/* 0 for contoller polling mode */
	u8 type;	/* SPI/SSP/Micrwire */
	u8 enable_dma;
	u8 slave_enable;
};

/**
 * rockchip_spi_set_platdata - SPI Controller configure callback by the board
 *				initialization code.
 * @cfg_gpio: Pointer to gpio setup function.
 * @src_clk_nr: Clock the SPI controller is to use to generate SPI clocks.
 * @num_cs: Number of elements in the 'cs' array.
 *
 * Call this from machine init code for each SPI Controller that
 * has some chips attached to it.
 */
extern void rockchip_spi0_set_platdata(int (*cfg_gpio)(void), int src_clk_nr,
						int num_cs);
extern void rockchip_spi1_set_platdata(int (*cfg_gpio)(void), int src_clk_nr,
						int num_cs);
extern void rockchip_spi2_set_platdata(int (*cfg_gpio)(void), int src_clk_nr,
						int num_cs);

/* defined by architecture to configure gpio */
extern int rockchip_spi0_cfg_gpio(void);
extern int rockchip_spi1_cfg_gpio(void);
extern int rockchip_spi2_cfg_gpio(void);

extern struct rockchip_spi_info rockchip_spi0_pdata;
extern struct rockchip_spi_info rockchip_spi1_pdata;
extern struct rockchip_spi_info rockchip_spi2_pdata;
#endif /* __ROCKCHIP_PLAT_SPI_H */
