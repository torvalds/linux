/*	$OpenBSD: pca9532.c,v 1.5 2024/05/28 13:21:13 jsg Exp $ */
/*
 * Copyright (c) 2006 Dale Rahn <drahn@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/gpio.h>


#include <dev/i2c/i2cvar.h>
#include <dev/gpio/gpiovar.h>

#include "gpio.h"

/* driver for PCA 9532 */

#define PCALED_ADDR 0x60

#define PCALED_GPIO_NPINS 16

struct pcaled_softc {
	struct device sc_dev;
	i2c_tag_t sc_tag;
	int sc_addr;
	struct gpio_chipset_tag sc_gpio_gc;
	struct gpio_pin sc_gpio_pin[PCALED_GPIO_NPINS];

};

int pcaled_match(struct device *, void *, void *);
void pcaled_attach(struct device *, struct device *, void *);
int pcaled_gpio_pin_read(void *arg, int pin);
void pcaled_gpio_pin_write(void *arg, int pin, int value);
void pcaled_gpio_pin_ctl(void *arg, int pin, int flags);

const struct cfattach pcaled_ca = {
	sizeof(struct pcaled_softc), pcaled_match, pcaled_attach
};

struct cfdriver pcaled_cd = {
	NULL, "pcaled", DV_DULL
};

int
pcaled_match(struct device *parent, void *v, void *arg)
{
	struct i2c_attach_args *ia = arg;
	int ok = 0;
	uint8_t cmd, data;

	if (ia->ia_addr != PCALED_ADDR)
		return (0);
	/* attempt to read input register 0 */
	iic_acquire_bus(ia->ia_tag, I2C_F_POLL);
	cmd = 0;
	if (iic_exec(ia->ia_tag, I2C_OP_READ_WITH_STOP, ia->ia_addr,
	    &cmd, sizeof cmd, &data, sizeof data, I2C_F_POLL))
		goto fail;
	cmd = 9;
	if (iic_exec(ia->ia_tag, I2C_OP_READ_WITH_STOP, ia->ia_addr,
	    &cmd, sizeof cmd, &data, sizeof data, I2C_F_POLL))
		goto fail;
	ok = 1;
fail:
	iic_release_bus(ia->ia_tag, I2C_F_POLL);
	return (ok);
}

void
pcaled_attach(struct device *parent, struct device *self, void *arg)
{
	struct pcaled_softc *sc = (void *)self;
	struct i2c_attach_args *ia = arg;
	struct gpiobus_attach_args gba;
	int i;
	uint8_t cmd, data;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);

	for (i = 0; i < PCALED_GPIO_NPINS; i++) {
		sc->sc_gpio_pin[i].pin_num = i;
		sc->sc_gpio_pin[i].pin_caps = GPIO_PIN_INOUT;
		if (i < 8)
			cmd = 0;
		else
			cmd = 1;
		if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
		    &cmd, sizeof cmd, &data, sizeof data, I2C_F_POLL))
			goto fail; /* XXX */
		sc->sc_gpio_pin[i].pin_state = (data >> (i & 3)) & 1;
	}
	sc->sc_gpio_gc.gp_cookie = sc;
	sc->sc_gpio_gc.gp_pin_read = pcaled_gpio_pin_read;
	sc->sc_gpio_gc.gp_pin_write = pcaled_gpio_pin_write;
	sc->sc_gpio_gc.gp_pin_ctl = pcaled_gpio_pin_ctl;

	printf(": PCA9532 LED controller\n");

	gba.gba_name = "gpio";
	gba.gba_gc = &sc->sc_gpio_gc;
	gba.gba_pins = sc->sc_gpio_pin;
	gba.gba_npins = PCALED_GPIO_NPINS;
#if NGPIO > 0
	config_found(&sc->sc_dev, &gba, gpiobus_print);
#endif

fail:
	iic_release_bus(sc->sc_tag, I2C_F_POLL);
}

int
pcaled_gpio_pin_read(void *arg, int pin)
{
	struct pcaled_softc *sc = arg;
	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	uint8_t cmd, data;

	if (pin < 8)
		cmd = 0;
	else
		cmd = 1;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, I2C_F_POLL))
		goto fail; /* XXX */

fail:
	iic_release_bus(sc->sc_tag, I2C_F_POLL);
	return (data >> (pin & 3)) & 1;
}

void
pcaled_gpio_pin_write(void *arg, int pin, int value)
{
	struct pcaled_softc *sc = arg;
	uint8_t cmd, data;
	if (pin < 4)
		cmd = 6;
	else if (pin < 8)
		cmd = 7;
	else if (pin < 12)
		cmd = 8;
	else
		cmd = 9;
	if (iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, I2C_F_POLL))
		goto fail; /* XXX */
	data &= ~(0x3 << (2*(pin & 3)));
	data |= (value << (2*(pin & 3)));

	if (iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &data, sizeof data, I2C_F_POLL))
		goto fail; /* XXX */

fail:
	iic_release_bus(sc->sc_tag, I2C_F_POLL);
}

void
pcaled_gpio_pin_ctl(void *arg, int pin, int flags)
{
	/* XXX all pins are inout */ 
}

