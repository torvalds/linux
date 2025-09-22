/*	$OpenBSD: gpio.h,v 1.8 2011/10/03 20:24:51 matthieu Exp $	*/
/*
 * Copyright (c) 2004 Alexander Yurchenko <grange@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SYS_GPIO_H_
#define _SYS_GPIO_H_

/* GPIO pin states */
#define GPIO_PIN_LOW		0x00	/* low level (logical 0) */
#define GPIO_PIN_HIGH		0x01	/* high level (logical 1) */

/* Max name length of a pin */
#define GPIOPINMAXNAME		64

/* GPIO pin configuration flags */
#define GPIO_PIN_INPUT		0x0001	/* input direction */
#define GPIO_PIN_OUTPUT		0x0002	/* output direction */
#define GPIO_PIN_INOUT		0x0004	/* bi-directional */
#define GPIO_PIN_OPENDRAIN	0x0008	/* open-drain output */
#define GPIO_PIN_PUSHPULL	0x0010	/* push-pull output */
#define GPIO_PIN_TRISTATE	0x0020	/* output disabled */
#define GPIO_PIN_PULLUP		0x0040	/* internal pull-up enabled */
#define GPIO_PIN_PULLDOWN	0x0080	/* internal pull-down enabled */
#define GPIO_PIN_INVIN		0x0100	/* invert input */
#define GPIO_PIN_INVOUT		0x0200	/* invert output */
#define GPIO_PIN_USER		0x0400	/* user != 0 can access */
#define GPIO_PIN_SET		0x8000	/* set for securelevel access */

/* GPIO controller description */
struct gpio_info {
	int gpio_npins;		/* total number of pins available */
};

/* GPIO pin operation (read/write/toggle) */
struct gpio_pin_op {
	char gp_name[GPIOPINMAXNAME];	/* pin name */
	int gp_pin;			/* pin number */
	int gp_value;			/* value */
};

/* GPIO pin configuration */
struct gpio_pin_set {
	char gp_name[GPIOPINMAXNAME];
	int gp_pin;
	int gp_caps;
	int gp_flags;
	char gp_name2[GPIOPINMAXNAME];	/* new name */
};

/* Attach/detach device drivers that use GPIO pins */
struct gpio_attach {
	char ga_dvname[16];	/* device name */
	int ga_offset;		/* pin number */
	u_int32_t ga_mask;	/* binary mask */
	u_int32_t ga_flags;	/* flags */
};

#define GPIOINFO		_IOR('G', 0, struct gpio_info)
#define GPIOPINREAD		_IOWR('G', 1, struct gpio_pin_op)
#define GPIOPINWRITE		_IOWR('G', 2, struct gpio_pin_op)
#define GPIOPINTOGGLE		_IOWR('G', 3, struct gpio_pin_op)
#define GPIOPINSET		_IOWR('G', 4, struct gpio_pin_set)
#define GPIOPINUNSET		_IOWR('G', 5, struct gpio_pin_set)
#define GPIOATTACH		_IOWR('G', 6, struct gpio_attach)
#define GPIODETACH		_IOWR('G', 7, struct gpio_attach)

#endif	/* !_SYS_GPIO_H_ */
