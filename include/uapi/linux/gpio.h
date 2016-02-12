/*
 * <linux/gpio.h> - userspace ABI for the GPIO character devices
 *
 * Copyright (C) 2015 Linus Walleij
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef _UAPI_GPIO_H_
#define _UAPI_GPIO_H_

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * struct gpiochip_info - Information about a certain GPIO chip
 * @name: the name of this GPIO chip
 * @label: a functional name for this GPIO chip
 * @lines: number of GPIO lines on this chip
 */
struct gpiochip_info {
	char name[32];
	char label[32];
	__u32 lines;
};

#define GPIO_GET_CHIPINFO_IOCTL _IOR('o', 0x01, struct gpiochip_info)

#endif /* _UAPI_GPIO_H_ */
