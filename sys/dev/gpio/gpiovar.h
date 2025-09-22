/*	$OpenBSD: gpiovar.h,v 1.6 2011/10/03 20:24:51 matthieu Exp $	*/

/*
 * Copyright (c) 2004, 2006 Alexander Yurchenko <grange@openbsd.org>
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

#ifndef _DEV_GPIO_GPIOVAR_H_
#define _DEV_GPIO_GPIOVAR_H_

/* GPIO controller description */
typedef struct gpio_chipset_tag {
	void	*gp_cookie;

	int	(*gp_pin_read)(void *, int);
	void	(*gp_pin_write)(void *, int, int);
	void	(*gp_pin_ctl)(void *, int, int);
} *gpio_chipset_tag_t;

/* GPIO pin description */
typedef struct gpio_pin {
	int	pin_num;		/* number */
	int	pin_caps;		/* capabilities */
	int	pin_flags;		/* current configuration */
	int	pin_state;		/* current state */
	int	pin_mapped;		/* is mapped */
} gpio_pin_t;

/* Attach GPIO framework to the controller */
struct gpiobus_attach_args {
	const char		*gba_name;	/* bus name */
	gpio_chipset_tag_t	gba_gc;		/* underlying controller */
	gpio_pin_t		*gba_pins;	/* pins array */
	int			gba_npins;	/* total number of pins */
};

int gpiobus_print(void *, const char *);

/* GPIO framework private methods */
#define gpiobus_pin_read(gc, pin) \
    ((gc)->gp_pin_read((gc)->gp_cookie, (pin)))
#define gpiobus_pin_write(gc, pin, value) \
    ((gc)->gp_pin_write((gc)->gp_cookie, (pin), (value)))
#define gpiobus_pin_ctl(gc, pin, flags) \
    ((gc)->gp_pin_ctl((gc)->gp_cookie, (pin), (flags)))

/* Attach devices connected to the GPIO pins */
struct gpio_attach_args {
	void *			ga_gpio;
	int			ga_offset;
	u_int32_t		ga_mask;
	char			*ga_dvname;
	u_int32_t		ga_flags;
};

/* GPIO pin map */
struct gpio_pinmap {
	int *	pm_map;			/* pin map */
	int	pm_size;		/* map size */
};

struct gpio_dev {
	struct device		*sc_dev;	/* the gpio device */
	LIST_ENTRY(gpio_dev)	 sc_next;
};

struct gpio_name {
	char			gp_name[GPIOPINMAXNAME];
	int			gp_pin;
	LIST_ENTRY(gpio_name)	gp_next;
};

int	gpio_pin_map(void *, int, u_int32_t, struct gpio_pinmap *);
void	gpio_pin_unmap(void *, struct gpio_pinmap *);
int	gpio_pin_read(void *, struct gpio_pinmap *, int);
void	gpio_pin_write(void *, struct gpio_pinmap *, int, int);
void	gpio_pin_ctl(void *, struct gpio_pinmap *, int, int);
int	gpio_pin_caps(void *, struct gpio_pinmap *, int);

int	gpio_npins(u_int32_t);

#endif	/* !_DEV_GPIO_GPIOVAR_H_ */
