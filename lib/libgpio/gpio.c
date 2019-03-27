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
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/types.h>

#include <libgpio.h>

gpio_handle_t
gpio_open(unsigned int unit)
{
	char device[16];

	snprintf(device, sizeof(device), "/dev/gpioc%u", unit);

	return (gpio_open_device(device));
}

gpio_handle_t
gpio_open_device(const char *device)
{
	int fd, maxpins;
	int serr;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		return (GPIO_INVALID_HANDLE);
	/*
	 * Check whether a simple ioctl works.
	 */
	if (ioctl(fd, GPIOMAXPIN, &maxpins) < 0) {
		serr = errno;
		close(fd);
		errno = serr;
		return (GPIO_INVALID_HANDLE);
	}

	return (fd);
}

void
gpio_close(gpio_handle_t handle)
{
	close(handle);
}

int
gpio_pin_list(gpio_handle_t handle, gpio_config_t **pcfgs)
{
	int maxpins, i;
	gpio_config_t *cfgs;

	*pcfgs = NULL;
	if (ioctl(handle, GPIOMAXPIN, &maxpins) < 0)
		return (-1);
	/* Reasonable values. */
	if (maxpins < 0 || maxpins > 4096) {
		errno = EINVAL;
		return (-1);
	}
	cfgs = calloc(maxpins + 1, sizeof(*cfgs));
	if (cfgs == NULL)
		return (-1);
	for (i = 0; i <= maxpins; i++) {
		cfgs[i].g_pin = i;
		gpio_pin_config(handle, &cfgs[i]);
	}
	*pcfgs = cfgs;

	return (maxpins);
}

int
gpio_pin_config(gpio_handle_t handle, gpio_config_t *cfg)
{
	struct gpio_pin gppin;

	if (cfg == NULL)
		return (-1);
	gppin.gp_pin = cfg->g_pin;
	if (ioctl(handle, GPIOGETCONFIG, &gppin) < 0)
		return (-1);
	strlcpy(cfg->g_name, gppin.gp_name, GPIOMAXNAME);
	cfg->g_caps = gppin.gp_caps;
	cfg->g_flags = gppin.gp_flags;

	return (0);
}

int
gpio_pin_set_name(gpio_handle_t handle, gpio_pin_t pin, char *name)
{
	struct gpio_pin gppin;

	if (name == NULL)
		return (-1);
	bzero(&gppin, sizeof(gppin));
	gppin.gp_pin = pin;
	strlcpy(gppin.gp_name, name, GPIOMAXNAME);
	if (ioctl(handle, GPIOSETNAME, &gppin) < 0)
		return (-1);

	return (0);
}

int
gpio_pin_set_flags(gpio_handle_t handle, gpio_config_t *cfg)
{
	struct gpio_pin gppin;

	if (cfg == NULL)
		return (-1);
	gppin.gp_pin = cfg->g_pin;
	gppin.gp_flags = cfg->g_flags;
	if (ioctl(handle, GPIOSETCONFIG, &gppin) < 0)
		return (-1);

	return (0);
}

gpio_value_t
gpio_pin_get(gpio_handle_t handle, gpio_pin_t pin)
{
	struct gpio_req gpreq;

	bzero(&gpreq, sizeof(gpreq));
	gpreq.gp_pin = pin;
	if (ioctl(handle, GPIOGET, &gpreq) < 0)
		return (GPIO_VALUE_INVALID);

	return (gpreq.gp_value);
}

int
gpio_pin_set(gpio_handle_t handle, gpio_pin_t pin, gpio_value_t value)
{
	struct gpio_req gpreq;

	if (value == GPIO_VALUE_INVALID)
		return (-1);
	bzero(&gpreq, sizeof(gpreq));
	gpreq.gp_pin = pin;
	gpreq.gp_value = value;
	if (ioctl(handle, GPIOSET, &gpreq) < 0)
		return (-1);

	return (0);
}

int
gpio_pin_toggle(gpio_handle_t handle, gpio_pin_t pin)
{
	struct gpio_req gpreq;

	bzero(&gpreq, sizeof(gpreq));
	gpreq.gp_pin = pin;
	if (ioctl(handle, GPIOTOGGLE, &gpreq) < 0)
		return (-1);

	return (0);
}

int
gpio_pin_low(gpio_handle_t handle, gpio_pin_t pin)
{
	return (gpio_pin_set(handle, pin, GPIO_VALUE_LOW));
}

int
gpio_pin_high(gpio_handle_t handle, gpio_pin_t pin)
{
	return (gpio_pin_set(handle, pin, GPIO_VALUE_HIGH));
}

static int
gpio_pin_set_flag(gpio_handle_t handle, gpio_pin_t pin, uint32_t flag)
{
	gpio_config_t cfg;

	bzero(&cfg, sizeof(cfg));
	cfg.g_pin = pin;
	if (gpio_pin_config(handle, &cfg) < 0)
		return (-1);
	cfg.g_flags = flag;

	return (gpio_pin_set_flags(handle, &cfg));
}

int
gpio_pin_input(gpio_handle_t handle, gpio_pin_t pin)
{
	return (gpio_pin_set_flag(handle, pin, GPIO_PIN_INPUT));
}

int
gpio_pin_output(gpio_handle_t handle, gpio_pin_t pin)
{
	return (gpio_pin_set_flag(handle, pin, GPIO_PIN_OUTPUT));
}

int
gpio_pin_opendrain(gpio_handle_t handle, gpio_pin_t pin)
{
	return (gpio_pin_set_flag(handle, pin, GPIO_PIN_OPENDRAIN));
}

int
gpio_pin_pushpull(gpio_handle_t handle, gpio_pin_t pin)
{
	return (gpio_pin_set_flag(handle, pin, GPIO_PIN_PUSHPULL));
}

int
gpio_pin_tristate(gpio_handle_t handle, gpio_pin_t pin)
{
	return (gpio_pin_set_flag(handle, pin, GPIO_PIN_TRISTATE));
}

int
gpio_pin_pullup(gpio_handle_t handle, gpio_pin_t pin)
{
	return (gpio_pin_set_flag(handle, pin, GPIO_PIN_PULLUP));
}

int
gpio_pin_pulldown(gpio_handle_t handle, gpio_pin_t pin)
{
	return (gpio_pin_set_flag(handle, pin, GPIO_PIN_PULLDOWN));
}

int
gpio_pin_invin(gpio_handle_t handle, gpio_pin_t pin)
{
	return (gpio_pin_set_flag(handle, pin, GPIO_PIN_INVIN));
}

int
gpio_pin_invout(gpio_handle_t handle, gpio_pin_t pin)
{
	return (gpio_pin_set_flag(handle, pin, GPIO_PIN_INVOUT));
}

int
gpio_pin_pulsate(gpio_handle_t handle, gpio_pin_t pin)
{
	return (gpio_pin_set_flag(handle, pin, GPIO_PIN_PULSATE));
}
