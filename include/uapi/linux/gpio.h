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

/* Line is in use by the kernel */
#define GPIOLINE_FLAG_KERNEL		(1UL << 0)
#define GPIOLINE_FLAG_IS_OUT		(1UL << 1)
#define GPIOLINE_FLAG_ACTIVE_LOW	(1UL << 2)
#define GPIOLINE_FLAG_OPEN_DRAIN	(1UL << 3)
#define GPIOLINE_FLAG_OPEN_SOURCE	(1UL << 4)

/**
 * struct gpioline_info - Information about a certain GPIO line
 * @line_offset: the local offset on this GPIO device, fill in when
 * requesting information from the kernel
 * @flags: various flags for this line
 * @name: the name of this GPIO line
 * @label: a functional name for this GPIO line
 * @kernel: this GPIO is in use by the kernel
 * @out: this GPIO is an output line (false means it is an input)
 * @active_low: this GPIO is active low
 */
struct gpioline_info {
	__u32 line_offset;
	__u32 flags;
	char name[32];
	char label[32];
};

#define GPIO_GET_CHIPINFO_IOCTL _IOR('o', 0x01, struct gpiochip_info)
#define GPIO_GET_LINEINFO_IOCTL _IOWR('o', 0x02, struct gpioline_info)

#endif /* _UAPI_GPIO_H_ */
