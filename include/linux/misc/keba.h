/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2024, KEBA Industrial Automation Gmbh */

#ifndef _LINUX_MISC_KEBA_H
#define _LINUX_MISC_KEBA_H

#include <linux/auxiliary_bus.h>

struct i2c_board_info;
struct spi_board_info;

/**
 * struct keba_i2c_auxdev - KEBA I2C auxiliary device
 * @auxdev: auxiliary device object
 * @io: address range of I2C controller IO memory
 * @info_size: number of I2C devices to be probed
 * @info: I2C devices to be probed
 */
struct keba_i2c_auxdev {
	struct auxiliary_device auxdev;
	struct resource io;
	int info_size;
	struct i2c_board_info *info;
};

/**
 * struct keba_spi_auxdev - KEBA SPI auxiliary device
 * @auxdev: auxiliary device object
 * @io: address range of SPI controller IO memory
 * @info_size: number of SPI devices to be probed
 * @info: SPI devices to be probed
 */
struct keba_spi_auxdev {
	struct auxiliary_device auxdev;
	struct resource io;
	int info_size;
	struct spi_board_info *info;
};

/**
 * struct keba_fan_auxdev - KEBA fan auxiliary device
 * @auxdev: auxiliary device object
 * @io: address range of fan controller IO memory
 */
struct keba_fan_auxdev {
	struct auxiliary_device auxdev;
	struct resource io;
};

/**
 * struct keba_batt_auxdev - KEBA battery auxiliary device
 * @auxdev: auxiliary device object
 * @io: address range of battery controller IO memory
 */
struct keba_batt_auxdev {
	struct auxiliary_device auxdev;
	struct resource io;
};

/**
 * struct keba_uart_auxdev - KEBA UART auxiliary device
 * @auxdev: auxiliary device object
 * @io: address range of UART controller IO memory
 * @irq: number of UART controller interrupt
 */
struct keba_uart_auxdev {
	struct auxiliary_device auxdev;
	struct resource io;
	unsigned int irq;
};

#endif /* _LINUX_MISC_KEBA_H */
