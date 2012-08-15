/*
 * Copyright (C) 2007-2012 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * GPIO block resgister definitions and inline macros for
 * U300 GPIO COH 901 335 or COH 901 571/3
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */

#ifndef __MACH_U300_GPIO_U300_H
#define __MACH_U300_GPIO_U300_H

/**
 * struct u300_gpio_platform - U300 GPIO platform data
 * @ports: number of GPIO block ports
 * @gpio_base: first GPIO number for this block (use a free range)
 * @gpio_irq_base: first GPIO IRQ number for this block (use a free range)
 * @pinctrl_device: pin control device to spawn as child
 */
struct u300_gpio_platform {
	u8 ports;
	int gpio_base;
	int gpio_irq_base;
	struct platform_device *pinctrl_device;
};

#endif /* __MACH_U300_GPIO_U300_H */
