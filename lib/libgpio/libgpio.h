/*-
 * Copyright (c) 2013-2014 Rui Paulo <rpaulo@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _LIBGPIO_H_
#define _LIBGPIO_H_

#include <sys/gpio.h>

__BEGIN_DECLS

#define	GPIO_INVALID_HANDLE -1
typedef int gpio_handle_t;
typedef uint32_t gpio_pin_t;

/*
 * Structure describing a GPIO pin configuration.
 */
typedef struct {
	gpio_pin_t	g_pin;
	char 		g_name[GPIOMAXNAME];
	uint32_t	g_caps;
	uint32_t	g_flags;
} gpio_config_t;

typedef enum {
	GPIO_VALUE_INVALID 	= -1,
	GPIO_VALUE_LOW 		= GPIO_PIN_LOW,
	GPIO_VALUE_HIGH 	= GPIO_PIN_HIGH
} gpio_value_t;

/*
 * Open /dev/gpiocN or a specific device.
 */
gpio_handle_t	gpio_open(unsigned int);
gpio_handle_t	gpio_open_device(const char *);
void		gpio_close(gpio_handle_t);
/*
 * Get a list of all the GPIO pins.
 */
int		gpio_pin_list(gpio_handle_t, gpio_config_t **);
/*
 * GPIO pin configuration.
 *
 * Retrieve the configuration of a specific GPIO pin.  The pin number is
 * passed through the gpio_config_t structure.
 */
int		gpio_pin_config(gpio_handle_t, gpio_config_t *);
/*
 * Sets the GPIO pin name.  The pin number and pin name to be set are passed
 * as parameters.
 */
int		gpio_pin_set_name(gpio_handle_t, gpio_pin_t, char *);
/*
 * Sets the GPIO flags on a specific GPIO pin.  The pin number and the flags
 * to be set are passed through the gpio_config_t structure.
 */
int		gpio_pin_set_flags(gpio_handle_t, gpio_config_t *);
/*
 * GPIO pin values.
 */
int		gpio_pin_get(gpio_handle_t, gpio_pin_t);
int		gpio_pin_set(gpio_handle_t, gpio_pin_t, int);
int		gpio_pin_toggle(gpio_handle_t, gpio_pin_t);
/*
 * Helper functions to set pin states.
 */
int		gpio_pin_low(gpio_handle_t, gpio_pin_t);
int		gpio_pin_high(gpio_handle_t, gpio_pin_t);
/*
 * Helper functions to configure pins.
 */
int		gpio_pin_input(gpio_handle_t, gpio_pin_t);
int		gpio_pin_output(gpio_handle_t, gpio_pin_t);
int		gpio_pin_opendrain(gpio_handle_t, gpio_pin_t);
int		gpio_pin_pushpull(gpio_handle_t, gpio_pin_t);
int		gpio_pin_tristate(gpio_handle_t, gpio_pin_t);
int		gpio_pin_pullup(gpio_handle_t, gpio_pin_t);
int		gpio_pin_pulldown(gpio_handle_t, gpio_pin_t);
int		gpio_pin_invin(gpio_handle_t, gpio_pin_t);
int		gpio_pin_invout(gpio_handle_t, gpio_pin_t);
int		gpio_pin_pulsate(gpio_handle_t, gpio_pin_t);

__END_DECLS

#endif /* _LIBGPIO_H_ */
