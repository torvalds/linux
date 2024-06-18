/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * i2c-gpio interface to platform code
 *
 * Copyright (C) 2007 Atmel Corporation
 */
#ifndef _LINUX_I2C_GPIO_H
#define _LINUX_I2C_GPIO_H

/**
 * struct i2c_gpio_platform_data - Platform-dependent data for i2c-gpio
 * @udelay: signal toggle delay. SCL frequency is (500 / udelay) kHz
 * @timeout: clock stretching timeout in jiffies. If the slave keeps
 *	SCL low for longer than this, the transfer will time out.
 * @sda_is_open_drain: SDA is configured as open drain, i.e. the pin
 *	isn't actively driven high when setting the output value high.
 *	gpio_get_value() must return the actual pin state even if the
 *	pin is configured as an output.
 * @sda_is_output_only: SDA output drivers can't be turned off.
 *	This is for clients that can only read SDA/SCL.
 * @sda_has_no_pullup: SDA is used in a non-compliant way and has no pull-up.
 *	Therefore disable open-drain.
 * @scl_is_open_drain: SCL is set up as open drain. Same requirements
 *	as for sda_is_open_drain apply.
 * @scl_is_output_only: SCL output drivers cannot be turned off.
 * @scl_has_no_pullup: SCL is used in a non-compliant way and has no pull-up.
 *	Therefore disable open-drain.
 */
struct i2c_gpio_platform_data {
	int		udelay;
	int		timeout;
	unsigned int	sda_is_open_drain:1;
	unsigned int	sda_is_output_only:1;
	unsigned int	sda_has_no_pullup:1;
	unsigned int	scl_is_open_drain:1;
	unsigned int	scl_is_output_only:1;
	unsigned int	scl_has_no_pullup:1;
};

#endif /* _LINUX_I2C_GPIO_H */
