/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * timb_gpio.h timberdale FPGA GPIO driver, platform data definition
 * Copyright (c) 2009 Intel Corporation
 */

#ifndef _LINUX_TIMB_GPIO_H
#define _LINUX_TIMB_GPIO_H

/**
 * struct timbgpio_platform_data - Platform data of the Timberdale GPIO driver
 * @gpio_base		The number of the first GPIO pin, set to -1 for
 *			dynamic number allocation.
 * @nr_pins		Number of pins that is supported by the hardware (1-32)
 * @irq_base		If IRQ is supported by the hardware, this is the base
 *			number of IRQ:s. One IRQ per pin will be used. Set to
 *			-1 if IRQ:s is not supported.
 */
struct timbgpio_platform_data {
	int gpio_base;
	int nr_pins;
	int irq_base;
};

#endif
