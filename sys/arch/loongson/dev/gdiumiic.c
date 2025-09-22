/*	$OpenBSD: gdiumiic.c,v 1.9 2022/08/10 15:00:58 miod Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
 * Copyright (c) 2006 Alexander Yurchenko <grange@openbsd.org>
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

/*
 * I2C bus bit-banging through GPIO pins.
 *
 * Derived from /sys/dev/gpio/gpioiic.c, with SDA and SCL pin order
 * exchanged, and knowledge of bus contents added.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/gpio.h>
#include <sys/rwlock.h>

#include <machine/autoconf.h>

#include <dev/gpio/gpiovar.h>

#include <dev/i2c/i2cvar.h>
#include <dev/i2c/i2c_bitbang.h>

#define GPIOIIC_PIN_SDA		1
#define GPIOIIC_PIN_SCL		0
#define GPIOIIC_NPINS		2

#define GPIOIIC_SDA		0x02
#define GPIOIIC_SCL		0x01

struct gdiumiic_softc {
	struct device		sc_dev;

	void *			sc_gpio;
	struct gpio_pinmap	sc_map;
	int			__map[GPIOIIC_NPINS];

	struct i2c_controller	sc_i2c_tag;
	struct rwlock		sc_i2c_lock;

	int			sc_sda;
	int			sc_scl;
};

int		gdiumiic_match(struct device *, void *, void *);
void		gdiumiic_attach(struct device *, struct device *, void *);
int		gdiumiic_detach(struct device *, int);

int		gdiumiic_i2c_acquire_bus(void *, int);
void		gdiumiic_i2c_release_bus(void *, int);
int		gdiumiic_i2c_send_start(void *, int);
int		gdiumiic_i2c_send_stop(void *, int);
int		gdiumiic_i2c_initiate_xfer(void *, i2c_addr_t, int);
int		gdiumiic_i2c_read_byte(void *, u_int8_t *, int);
int		gdiumiic_i2c_write_byte(void *, u_int8_t, int);

void		gdiumiic_bb_set_bits(void *, u_int32_t);
void		gdiumiic_bb_set_dir(void *, u_int32_t);
u_int32_t	gdiumiic_bb_read_bits(void *);

int		gdiumiic_bustype(struct gpio_attach_args *);
void		gdiumiic_sensors_scan(struct device *,
		    struct i2cbus_attach_args *, void *);

const struct cfattach gdiumiic_ca = {
	sizeof(struct gdiumiic_softc),
	gdiumiic_match,
	gdiumiic_attach,
	gdiumiic_detach
};

struct cfdriver gdiumiic_cd = {
	NULL, "gdiumiic", DV_DULL
};

static const struct i2c_bitbang_ops gdiumiic_bbops = {
	gdiumiic_bb_set_bits,
	gdiumiic_bb_set_dir,
	gdiumiic_bb_read_bits,
	{ GPIOIIC_SDA, GPIOIIC_SCL, GPIOIIC_SDA, 0 }
};

#define	GDIUMIIC_BUSTYPE_SENSORS	0
#define	GDIUMIIC_BUSTYPE_VIDCTRL	1

int
gdiumiic_bustype(struct gpio_attach_args *ga)
{
	extern int gdium_revision;

	/*
	 * Hardware pin connections depend upon the motherboard revision.
	 * XXX magic numbers (should match kernel configuration)
	 */
	switch (gdium_revision) {
	case 0:
		if (ga->ga_offset == 46 && ga->ga_mask == 0x03)
			return GDIUMIIC_BUSTYPE_SENSORS;
		break;
	default:
		if (ga->ga_offset == 6 && ga->ga_mask == 0x81)
			return GDIUMIIC_BUSTYPE_SENSORS;
		break;
	}

	if (ga->ga_offset == 41 && ga->ga_mask == 0x03)
		return GDIUMIIC_BUSTYPE_VIDCTRL;

	return -1;

}

int
gdiumiic_match(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct gpio_attach_args *ga = aux;

	if (ga->ga_offset == -1 || gdiumiic_bustype(ga) < 0)
		return 0;

	return (sys_platform->system_type == LOONGSON_GDIUM &&
	    strcmp(cf->cf_driver->cd_name, "gdiumiic") == 0);
}

void
gdiumiic_attach(struct device *parent, struct device *self, void *aux)
{
	struct gdiumiic_softc *sc = (struct gdiumiic_softc *)self;
	struct gpio_attach_args *ga = aux;
	struct i2cbus_attach_args iba;
	int caps;

	/* Check that we have enough pins */
	if (gpio_npins(ga->ga_mask) != GPIOIIC_NPINS) {
		printf(": invalid pin mask\n");
		return;
	}

	/* Map pins */
	sc->sc_gpio = ga->ga_gpio;
	sc->sc_map.pm_map = sc->__map;
	if (gpio_pin_map(sc->sc_gpio, ga->ga_offset, ga->ga_mask,
	    &sc->sc_map)) {
		printf(": can't map pins\n");
		return;
	}

	/* Configure SDA pin */
	caps = gpio_pin_caps(sc->sc_gpio, &sc->sc_map, GPIOIIC_PIN_SDA);
	if (!(caps & GPIO_PIN_OUTPUT)) {
		printf(": SDA pin is unable to drive output\n");
		goto fail;
	}
	if (!(caps & GPIO_PIN_INPUT)) {
		printf(": SDA pin is unable to read input\n");
		goto fail;
	}
	printf(": SDA[%d]", sc->sc_map.pm_map[GPIOIIC_PIN_SDA]);
	sc->sc_sda = GPIO_PIN_OUTPUT;
#if 0
	if (caps & GPIO_PIN_OPENDRAIN) {
		printf(" open-drain");
		sc->sc_sda |= GPIO_PIN_OPENDRAIN;
	} else if ((caps & GPIO_PIN_PUSHPULL) && (caps & GPIO_PIN_TRISTATE)) {
		printf(" push-pull tri-state");
		sc->sc_sda |= GPIO_PIN_PUSHPULL;
	}
	if (caps & GPIO_PIN_PULLUP) {
		printf(" pull-up");
		sc->sc_sda |= GPIO_PIN_PULLUP;
	}
#endif
	gpio_pin_ctl(sc->sc_gpio, &sc->sc_map, GPIOIIC_PIN_SDA, sc->sc_sda);

	/* Configure SCL pin */
	caps = gpio_pin_caps(sc->sc_gpio, &sc->sc_map, GPIOIIC_PIN_SCL);
	if (!(caps & GPIO_PIN_OUTPUT)) {
		printf(": SCL pin is unable to drive output\n");
		goto fail;
	}
	printf(", SCL[%d]", sc->sc_map.pm_map[GPIOIIC_PIN_SCL]);
	sc->sc_scl = GPIO_PIN_OUTPUT;
#if 0
	if (caps & GPIO_PIN_OPENDRAIN) {
		printf(" open-drain");
		sc->sc_scl |= GPIO_PIN_OPENDRAIN;
		if (caps & GPIO_PIN_PULLUP) {
			printf(" pull-up");
			sc->sc_scl |= GPIO_PIN_PULLUP;
		}
	} else if (caps & GPIO_PIN_PUSHPULL) {
		printf(" push-pull");
		sc->sc_scl |= GPIO_PIN_PUSHPULL;
	}
#endif
	gpio_pin_ctl(sc->sc_gpio, &sc->sc_map, GPIOIIC_PIN_SCL, sc->sc_scl);

	printf("\n");

	/* Attach I2C bus */
	rw_init(&sc->sc_i2c_lock, "iiclk");
	sc->sc_i2c_tag.ic_cookie = sc;
	sc->sc_i2c_tag.ic_acquire_bus = gdiumiic_i2c_acquire_bus;
	sc->sc_i2c_tag.ic_release_bus = gdiumiic_i2c_release_bus;
	sc->sc_i2c_tag.ic_send_start = gdiumiic_i2c_send_start;
	sc->sc_i2c_tag.ic_send_stop = gdiumiic_i2c_send_stop;
	sc->sc_i2c_tag.ic_initiate_xfer = gdiumiic_i2c_initiate_xfer;
	sc->sc_i2c_tag.ic_read_byte = gdiumiic_i2c_read_byte;
	sc->sc_i2c_tag.ic_write_byte = gdiumiic_i2c_write_byte;

	bzero(&iba, sizeof(iba));
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_i2c_tag;
	if (gdiumiic_bustype(ga) == GDIUMIIC_BUSTYPE_SENSORS)
		iba.iba_bus_scan = gdiumiic_sensors_scan;
	config_found(self, &iba, iicbus_print);

	return;

fail:
	gpio_pin_unmap(sc->sc_gpio, &sc->sc_map);
}

int
gdiumiic_detach(struct device *self, int flags)
{
	return (0);
}

int
gdiumiic_i2c_acquire_bus(void *cookie, int flags)
{
	struct gdiumiic_softc *sc = cookie;

	if (cold || (flags & I2C_F_POLL))
		return (0);

	return (rw_enter(&sc->sc_i2c_lock, RW_WRITE | RW_INTR));
}

void
gdiumiic_i2c_release_bus(void *cookie, int flags)
{
	struct gdiumiic_softc *sc = cookie;

	if (cold || (flags & I2C_F_POLL))
		return;

	rw_exit(&sc->sc_i2c_lock);
}

int
gdiumiic_i2c_send_start(void *cookie, int flags)
{
	return (i2c_bitbang_send_start(cookie, flags, &gdiumiic_bbops));
}

int
gdiumiic_i2c_send_stop(void *cookie, int flags)
{
	return (i2c_bitbang_send_stop(cookie, flags, &gdiumiic_bbops));
}

int
gdiumiic_i2c_initiate_xfer(void *cookie, i2c_addr_t addr, int flags)
{
	return (i2c_bitbang_initiate_xfer(cookie, addr, flags, &gdiumiic_bbops));
}

int
gdiumiic_i2c_read_byte(void *cookie, u_int8_t *bytep, int flags)
{
	return (i2c_bitbang_read_byte(cookie, bytep, flags, &gdiumiic_bbops));
}

int
gdiumiic_i2c_write_byte(void *cookie, u_int8_t byte, int flags)
{
	return (i2c_bitbang_write_byte(cookie, byte, flags, &gdiumiic_bbops));
}

void
gdiumiic_bb_set_bits(void *cookie, u_int32_t bits)
{
	struct gdiumiic_softc *sc = cookie;

	gpio_pin_write(sc->sc_gpio, &sc->sc_map, GPIOIIC_PIN_SDA,
	    bits & GPIOIIC_SDA ? GPIO_PIN_HIGH : GPIO_PIN_LOW);
	gpio_pin_write(sc->sc_gpio, &sc->sc_map, GPIOIIC_PIN_SCL,
	    bits & GPIOIIC_SCL ? GPIO_PIN_HIGH : GPIO_PIN_LOW);
}

void
gdiumiic_bb_set_dir(void *cookie, u_int32_t bits)
{
	struct gdiumiic_softc *sc = cookie;
	int sda = sc->sc_sda;

	sda &= ~(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | GPIO_PIN_TRISTATE);
	sda |= (bits & GPIOIIC_SDA ? GPIO_PIN_OUTPUT : GPIO_PIN_INPUT);
#if 0
	if ((sda & GPIO_PIN_PUSHPULL) && !(bits & GPIOIIC_SDA))
		sda |= GPIO_PIN_TRISTATE;
#endif
	if (sc->sc_sda != sda) {
		sc->sc_sda = sda;
		gpio_pin_ctl(sc->sc_gpio, &sc->sc_map, GPIOIIC_PIN_SDA,
		    sc->sc_sda);
	}
}

u_int32_t
gdiumiic_bb_read_bits(void *cookie)
{
	struct gdiumiic_softc *sc = cookie;
	u_int32_t bits = 0;

	if (gpio_pin_read(sc->sc_gpio, &sc->sc_map, GPIOIIC_PIN_SDA) ==
	    GPIO_PIN_HIGH)
		bits |= GPIOIIC_SDA;
	if (gpio_pin_read(sc->sc_gpio, &sc->sc_map, GPIOIIC_PIN_SCL) ==
	    GPIO_PIN_HIGH)
		bits |= GPIOIIC_SCL;

	return bits;
}

/*
 * Attach devices to the first (sensors + RTC) i2c bus.
 *
 * Note that the i2c scan performed by the MI i2c code will fail to
 * identify our lm75 chip correctly.
 */
void
gdiumiic_sensors_scan(struct device *iicdev, struct i2cbus_attach_args *iba,
    void *arg)
{
	struct i2c_attach_args ia;
	/* not worth #define'ing _I2C_PRIVATE for */
	extern int iic_print(void *, const char *);

	bzero(&ia, sizeof ia);
	ia.ia_tag = iba->iba_tag;
	ia.ia_addr = 0x40;
	ia.ia_size = 1;
	ia.ia_name = "stsec";
	config_found(iicdev, &ia, iic_print);

	bzero(&ia, sizeof ia);
	ia.ia_tag = iba->iba_tag;
	ia.ia_addr = 0x48;
	ia.ia_size = 1;
	ia.ia_name = "lm75";
	config_found(iicdev, &ia, iic_print);

	bzero(&ia, sizeof ia);
	ia.ia_tag = iba->iba_tag;
	ia.ia_addr = 0x51;
	ia.ia_size = 1;
	ia.ia_name = "spd";
	config_found(iicdev, &ia, iic_print);

	bzero(&ia, sizeof ia);
	ia.ia_tag = iba->iba_tag;
	ia.ia_addr = 0x68;
	ia.ia_size = 1;
	ia.ia_name = "st,m41t83";
	config_found(iicdev, &ia, iic_print);
}
