/*
 * include/linux/amlogic/gpio-amlogic.h
 *
 * Copyright (C) 2013 Amlogic, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_AMLOGIC_GPIO_AMLOGIC_H
#define __LINUX_AMLOGIC_GPIO_AMLOGIC_H

#define GPIO_REG_BIT(reg,bit) ((reg<<5)|bit)
#define GPIO_REG(value) ((value>>5))
#define GPIO_BIT(value) ((value&0x1F))

struct amlogic_gpio_desc {
	unsigned num;
	char *name;
	unsigned int out_en_reg_bit;
	unsigned int out_value_reg_bit;
	unsigned int input_value_reg_bit;
	unsigned int map_to_irq;
	unsigned int pull_up_reg_bit;
	const char *gpio_owner;
};

struct amlogic_set_pullup {
	 int (*meson_set_pullup)(unsigned int,unsigned int,unsigned int);
};

#endif // __LINUX_AMLOGIC_GPIO_AMLOGIC_H