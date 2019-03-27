/* $NetBSD: gpio.h,v 1.7 2009/09/25 20:27:50 mbalmer Exp $ */
/*	$OpenBSD: gpio.h,v 1.7 2008/11/26 14:51:20 mbalmer Exp $	*/
/*-
 * SPDX-License-Identifier: (BSD-2-Clause-FreeBSD AND ISC)
 *
 * Copyright (c) 2009, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

/*
 * Copyright (c) 2009 Marc Balmer <marc@msys.ch>
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

#ifndef __GPIO_H__
#define __GPIO_H__

#include <sys/ioccom.h>

/* GPIO pin states */
#define GPIO_PIN_LOW		0x00	/* low level (logical 0) */
#define GPIO_PIN_HIGH		0x01	/* high level (logical 1) */

/* Max name length of a pin */
#define GPIOMAXNAME		64

/* GPIO pin configuration flags */
#define GPIO_PIN_INPUT		0x00000001	/* input direction */
#define GPIO_PIN_OUTPUT		0x00000002	/* output direction */
#define GPIO_PIN_OPENDRAIN	0x00000004	/* open-drain output */
#define GPIO_PIN_PUSHPULL	0x00000008	/* push-pull output */
#define GPIO_PIN_TRISTATE	0x00000010	/* output disabled */
#define GPIO_PIN_PULLUP		0x00000020	/* internal pull-up enabled */
#define GPIO_PIN_PULLDOWN	0x00000040	/* internal pull-down enabled */
#define GPIO_PIN_INVIN		0x00000080	/* invert input */
#define GPIO_PIN_INVOUT		0x00000100	/* invert output */
#define GPIO_PIN_PULSATE	0x00000200	/* pulsate in hardware */
#define GPIO_PIN_PRESET_LOW	0x00000400	/* preset pin to high or */
#define GPIO_PIN_PRESET_HIGH	0x00000800	/* low before enabling output */
/* GPIO interrupt capabilities */
#define GPIO_INTR_NONE		0x00000000	/* no interrupt support */
#define GPIO_INTR_LEVEL_LOW	0x00010000	/* level trigger, low */
#define GPIO_INTR_LEVEL_HIGH	0x00020000	/* level trigger, high */
#define GPIO_INTR_EDGE_RISING	0x00040000	/* edge trigger, rising */
#define GPIO_INTR_EDGE_FALLING	0x00080000	/* edge trigger, falling */
#define GPIO_INTR_EDGE_BOTH	0x00100000	/* edge trigger, both */
#define GPIO_INTR_MASK		(GPIO_INTR_LEVEL_LOW | GPIO_INTR_LEVEL_HIGH | \
				GPIO_INTR_EDGE_RISING |			      \
				GPIO_INTR_EDGE_FALLING | GPIO_INTR_EDGE_BOTH)

struct gpio_pin {
	uint32_t gp_pin;			/* pin number */
	char gp_name[GPIOMAXNAME];		/* human-readable name */
	uint32_t gp_caps;			/* capabilities */
	uint32_t gp_flags;			/* current flags */
};

/* GPIO pin request (read/write/toggle) */
struct gpio_req {
	uint32_t gp_pin;			/* pin number */
	uint32_t gp_value;			/* value */
};

/*
 * gpio_access_32 / GPIOACCESS32
 *
 * Simultaneously read and/or change up to 32 adjacent pins.
 * If the device cannot change the pins simultaneously, returns EOPNOTSUPP.
 *
 * This accesses an adjacent set of up to 32 pins starting at first_pin within
 * the device's collection of pins.  How the hardware pins are mapped to the 32
 * bits in the arguments is device-specific.  It is expected that lower-numbered
 * pins in the device's number space map linearly to lower-ordered bits within
 * the 32-bit words (i.e., bit 0 is first_pin, bit 1 is first_pin+1, etc).
 * Other mappings are possible; know your device.
 *
 * Some devices may limit the value of first_pin to 0, or to multiples of 16 or
 * 32 or some other hardware-specific number; to access pin 2 would require
 * first_pin to be zero and then manipulate bit (1 << 2) in the 32-bit word.
 * Invalid values in first_pin result in an EINVAL error return.
 *
 * The starting state of the pins is captured and stored in orig_pins, then the
 * pins are set to ((starting_state & ~clear_pins) ^ change_pins). 
 *
 *   Clear  Change  Hardware pin after call
 *     0      0        No change
 *     0      1        Opposite of current value
 *     1      0        Cleared
 *     1      1        Set
 */
struct gpio_access_32 {
	uint32_t first_pin;	/* First pin in group of 32 adjacent */
	uint32_t clear_pins;	/* Pins are changed using: */
	uint32_t change_pins;	/* ((hwstate & ~clear_pins) ^ change_pins) */
	uint32_t orig_pins;	/* Returned hwstate of pins before change. */
};

/*
 * gpio_config_32 / GPIOCONFIG32
 *
 * Simultaneously configure up to 32 adjacent pins.  This is intended to change
 * the configuration of all the pins simultaneously, such that pins configured
 * for output all begin to drive the configured values simultaneously, but not
 * all hardware can do that, so the driver "does the best it can" in this
 * regard.  Notably unlike pin_access_32(), this does NOT fail if the pins
 * cannot be atomically configured; it is expected that callers understand the
 * hardware and have decided to live with any such limitations it may have.
 *
 * The pin_flags argument is an array of GPIO_PIN_xxxx flags.  If the array
 * contains any GPIO_PIN_OUTPUT flags, the driver will manipulate the hardware
 * such that all output pins become driven with the proper initial values
 * simultaneously if it can.  The elements in the array map to pins in the same
 * way that bits are mapped by pin_acces_32(), and the same restrictions may
 * apply.  For example, to configure pins 2 and 3 it may be necessary to set
 * first_pin to zero and only populate pin_flags[2] and pin_flags[3].  If a
 * given array entry doesn't contain GPIO_PIN_INPUT or GPIO_PIN_OUTPUT then no
 * configuration is done for that pin.
 *
 * Some devices may limit the value of first_pin to 0, or to multiples of 16 or
 * 32 or some other hardware-specific number.  Invalid values in first_pin or
 * num_pins result in an error return with errno set to EINVAL.
 */
struct gpio_config_32 {
	uint32_t first_pin;
	uint32_t num_pins;
	uint32_t pin_flags[32];
};

/*
 * ioctls
 */
#define GPIOMAXPIN		_IOR('G', 0, int)
#define	GPIOGETCONFIG		_IOWR('G', 1, struct gpio_pin)
#define	GPIOSETCONFIG		_IOW('G', 2, struct gpio_pin)
#define	GPIOGET			_IOWR('G', 3, struct gpio_req)
#define	GPIOSET			_IOW('G', 4, struct gpio_req)
#define	GPIOTOGGLE		_IOWR('G', 5, struct gpio_req)
#define	GPIOSETNAME		_IOW('G', 6, struct gpio_pin)
#define	GPIOACCESS32		_IOWR('G', 7, struct gpio_access_32)
#define	GPIOCONFIG32		_IOW('G', 8, struct gpio_config_32)

#endif /* __GPIO_H__ */
