/* $OpenBSD: omgpio.c,v 1.15 2023/03/05 14:45:07 patrick Exp $ */
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
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
#include <sys/evcount.h>
#include <sys/gpio.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <dev/gpio/gpiovar.h>

#include <armv7/omap/prcmvar.h>
#include <armv7/omap/omgpiovar.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>

#include "gpio.h"

/* OMAP3 registers */
#define	GPIO3_REVISION		0x00
#define	GPIO3_SYSCONFIG		0x10
#define	GPIO3_SYSSTATUS		0x14
#define	GPIO3_IRQSTATUS1	0x18
#define	GPIO3_IRQENABLE1	0x1C
#define	GPIO3_WAKEUPENABLE	0x20
#define	GPIO3_IRQSTATUS2	0x28
#define	GPIO3_IRQENABLE2	0x2C
#define	GPIO3_CTRL		0x30
#define	GPIO3_OE		0x34
#define	GPIO3_DATAIN		0x38
#define	GPIO3_DATAOUT		0x3C
#define	GPIO3_LEVELDETECT0	0x40
#define	GPIO3_LEVELDETECT1	0x44
#define	GPIO3_RISINGDETECT	0x48
#define	GPIO3_FALLINGDETECT	0x4C
#define	GPIO3_DEBOUNCENABLE	0x50
#define	GPIO3_DEBOUNCINGTIME	0x54
#define	GPIO3_CLEARIRQENABLE1	0x60
#define	GPIO3_SETIRQENABLE1	0x64
#define	GPIO3_CLEARIRQENABLE2	0x70
#define	GPIO3_SETIRQENABLE2	0x74
#define	GPIO3_CLEARWKUENA	0x80
#define	GPIO3_SETWKUENA		0x84
#define	GPIO3_CLEARDATAOUT	0x90
#define	GPIO3_SETDATAOUT	0x94

/* OMAP4 registers */
#define GPIO4_REVISION		0x00
#define GPIO4_SYSCONFIG		0x10
#define GPIO4_IRQSTATUS_RAW_0	0x24
#define GPIO4_IRQSTATUS_RAW_1	0x28
#define GPIO4_IRQSTATUS_0	0x2C
#define GPIO4_IRQSTATUS_1	0x30
#define GPIO4_IRQSTATUS_SET_0	0x34
#define GPIO4_IRQSTATUS_SET_1	0x38
#define GPIO4_IRQSTATUS_CLR_0	0x3C
#define GPIO4_IRQSTATUS_CLR_1	0x40
#define GPIO4_IRQWAKEN_0	0x44
#define GPIO4_IRQWAKEN_1	0x48
#define GPIO4_SYSSTATUS		0x114
#define GPIO4_WAKEUPENABLE	0x120
#define GPIO4_CTRL		0x130
#define GPIO4_OE		0x134
#define GPIO4_DATAIN		0x138
#define GPIO4_DATAOUT		0x13C
#define GPIO4_LEVELDETECT0	0x140
#define GPIO4_LEVELDETECT1	0x144
#define GPIO4_RISINGDETECT 	0x148
#define GPIO4_FALLINGDETECT	0x14C
#define GPIO4_DEBOUNCENABLE	0x150
#define GPIO4_DEBOUNCINGTIME	0x154
#define GPIO4_CLEARWKUPENA	0x180
#define GPIO4_SETWKUENA		0x184
#define GPIO4_CLEARDATAOUT	0x190
#define GPIO4_SETDATAOUT	0x194

/* AM335X registers */
#define GPIO_AM335X_REVISION		0x00
#define GPIO_AM335X_SYSCONFIG		0x10
#define GPIO_AM335X_IRQSTATUS_RAW_0	0x24
#define GPIO_AM335X_IRQSTATUS_RAW_1	0x28
#define GPIO_AM335X_IRQSTATUS_0		0x2C
#define GPIO_AM335X_IRQSTATUS_1		0x30
#define GPIO_AM335X_IRQSTATUS_SET_0	0x34
#define GPIO_AM335X_IRQSTATUS_SET_1	0x38
#define GPIO_AM335X_IRQSTATUS_CLR_0	0x3c
#define GPIO_AM335X_IRQSTATUS_CLR_1	0x40
#define GPIO_AM335X_IRQWAKEN_0		0x44
#define GPIO_AM335X_IRQWAKEN_1		0x48
#define GPIO_AM335X_SYSSTATUS		0x114
#define GPIO_AM335X_CTRL		0x130
#define GPIO_AM335X_OE			0x134
#define GPIO_AM335X_DATAIN		0x138
#define GPIO_AM335X_DATAOUT		0x13C
#define GPIO_AM335X_LEVELDETECT0	0x140
#define GPIO_AM335X_LEVELDETECT1	0x144
#define GPIO_AM335X_RISINGDETECT	0x148
#define GPIO_AM335X_FALLINGDETECT	0x14C
#define GPIO_AM335X_DEBOUNCENABLE	0x150
#define GPIO_AM335X_DEBOUNCINGTIME	0x154
#define GPIO_AM335X_CLEARDATAOUT	0x190
#define GPIO_AM335X_SETDATAOUT		0x194

#define GPIO_NUM_PINS		32

struct intrhand {
	int (*ih_func)(void *);		/* handler */
	void *ih_arg;			/* arg for handler */
	int ih_ipl;			/* IPL_* */
	int ih_irq;			/* IRQ number */
	int ih_gpio;			/* gpio pin */
	struct evcount	ih_count;
	char *ih_name;
};

struct omgpio_regs {
	u_int32_t	revision;
	u_int32_t	sysconfig;
	u_int32_t	irqstatus_raw0;		/* omap4/am335x only */
	u_int32_t	irqstatus_raw1;		/* omap4/am335x only */
	u_int32_t	irqstatus0;
	u_int32_t	irqstatus1;
	u_int32_t	irqstatus_set0;
	u_int32_t	irqstatus_set1;
	u_int32_t	irqstatus_clear0;
	u_int32_t	irqstatus_clear1;
	u_int32_t	irqwaken0;
	u_int32_t	irqwaken1;
	u_int32_t	sysstatus;
	u_int32_t	wakeupenable;		/* omap3/omap4 only */
	u_int32_t	ctrl;
	u_int32_t	oe;
	u_int32_t	datain;
	u_int32_t	dataout;
	u_int32_t	leveldetect0;
	u_int32_t	leveldetect1;
	u_int32_t	risingdetect;
	u_int32_t	fallingdetect;
	u_int32_t	debounceenable;
	u_int32_t	debouncingtime;
	u_int32_t	clearwkupena;		/* omap3/omap4 only */
	u_int32_t	setwkupena;		/* omap3/omap4 only */
	u_int32_t	cleardataout;
	u_int32_t	setdataout;
};

struct omgpio_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	void			*sc_ih_h;
	void			*sc_ih_l;
	int 			sc_max_il;
	int 			sc_min_il;
	int			sc_node;
	struct intrhand		*sc_handlers[GPIO_NUM_PINS];
	struct gpio_controller	sc_gc;
	int			sc_omap_ver;
	struct gpio_chipset_tag	sc_gpio_gc;
	gpio_pin_t		sc_gpio_pins[GPIO_NUM_PINS];
	struct omgpio_regs	sc_regs;
	int			(*sc_padconf_set_gpioflags)(uint32_t, uint32_t);
};

#define GPIO_PIN_TO_INST(x)	((x) >> 5)
#define GPIO_PIN_TO_OFFSET(x)	((x) & 0x1f)
#define DEVNAME(sc)	((sc)->sc_dev.dv_xname)
#define READ4(sc, reg)		omgpio_read4(sc, reg)
#define WRITE4(sc, reg, val)	omgpio_write4(sc, reg, val)

u_int32_t omgpio_read4(struct omgpio_softc *, u_int32_t);
void omgpio_write4(struct omgpio_softc *, u_int32_t, u_int32_t);
int omgpio_match(struct device *, void *, void *);
void omgpio_attach(struct device *, struct device *, void *);
void omgpio_recalc_interrupts(struct omgpio_softc *);
int omgpio_irq(void *);
int omgpio_irq_dummy(void *);
int omgpio_pin_dir_read(struct omgpio_softc *, unsigned int);
void omgpio_pin_dir_write(struct omgpio_softc *, unsigned int, unsigned int);

void	omgpio_config_pin(void *, uint32_t *, int);
int	omgpio_get_pin(void *, uint32_t *);
void	omgpio_set_pin(void *, uint32_t *, int);

const struct cfattach omgpio_ca = {
	sizeof (struct omgpio_softc), omgpio_match, omgpio_attach
};

struct cfdriver omgpio_cd = {
	NULL, "omgpio", DV_DULL
};

const char *omgpio_compatible[] = {
	"ti,omap3-gpio",
	"ti,omap4-gpio",
	NULL
};

u_int32_t
omgpio_read4(struct omgpio_softc *sc, u_int32_t reg)
{
	if(reg == -1)
		panic("%s: Invalid register address", DEVNAME(sc));

	return bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg));
}

void
omgpio_write4(struct omgpio_softc *sc, u_int32_t reg, u_int32_t val)
{
	if(reg == -1)
		panic("%s: Invalid register address", DEVNAME(sc));

	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val));
}

int
omgpio_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int i;

	for (i = 0; omgpio_compatible[i] != NULL; i++) {
		if (OF_is_compatible(faa->fa_node, omgpio_compatible[i]))
			return 1;
	}
	return 0;
}

void
omgpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct omgpio_softc *sc = (struct omgpio_softc *) self;
	struct gpiobus_attach_args gba;
	u_int32_t	rev;
	int		i, len, unit;
	char		hwmods[64];

	if (faa->fa_nreg < 1)
		return;

	unit = -1;
	if ((len = OF_getprop(faa->fa_node, "ti,hwmods", hwmods,
	    sizeof(hwmods))) == 6) {
		if ((strncmp(hwmods, "gpio", 4) == 0) &&
		    (hwmods[4] > '0') && (hwmods[4] <= '9'))
			unit = hwmods[4] - '1';
	}

	if (unit != -1)
		prcm_enablemodule(PRCM_GPIO0 + unit);

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", DEVNAME(sc));

	if (OF_is_compatible(faa->fa_node, "ti,omap3-gpio")) {
		sc->sc_padconf_set_gpioflags = NULL;
		sc->sc_regs.revision = GPIO3_REVISION;
		sc->sc_regs.sysconfig = GPIO3_SYSCONFIG;
		sc->sc_regs.irqstatus_raw0 = -1;
		sc->sc_regs.irqstatus_raw1 = -1;
		sc->sc_regs.irqstatus0 = GPIO3_IRQSTATUS1;
		sc->sc_regs.irqstatus1 = GPIO3_IRQSTATUS2;
		sc->sc_regs.irqstatus_set0 = GPIO3_SETIRQENABLE1;
		sc->sc_regs.irqstatus_set1 = GPIO3_SETIRQENABLE2;
		sc->sc_regs.irqstatus_clear0 = GPIO3_CLEARIRQENABLE1;
		sc->sc_regs.irqstatus_clear1 = GPIO3_CLEARIRQENABLE2;
		sc->sc_regs.irqwaken0 = -1;
		sc->sc_regs.irqwaken1 = -1;
		sc->sc_regs.sysstatus = GPIO3_SYSSTATUS;
		sc->sc_regs.wakeupenable = GPIO3_WAKEUPENABLE;
		sc->sc_regs.ctrl = GPIO3_CTRL;
		sc->sc_regs.oe = GPIO3_OE;
		sc->sc_regs.datain = GPIO3_DATAIN;
		sc->sc_regs.dataout = GPIO3_DATAOUT;
		sc->sc_regs.leveldetect0 = GPIO3_LEVELDETECT0;
		sc->sc_regs.leveldetect1 = GPIO3_LEVELDETECT1;
		sc->sc_regs.risingdetect = GPIO3_RISINGDETECT;
		sc->sc_regs.fallingdetect = GPIO3_FALLINGDETECT;
		sc->sc_regs.debounceenable = GPIO3_DEBOUNCENABLE;
		sc->sc_regs.debouncingtime = GPIO3_DEBOUNCINGTIME;
		sc->sc_regs.clearwkupena = GPIO3_CLEARWKUENA;
		sc->sc_regs.setwkupena = GPIO3_SETWKUENA;
		sc->sc_regs.cleardataout = GPIO3_CLEARDATAOUT;
		sc->sc_regs.setdataout = GPIO3_SETDATAOUT;
	} else if (OF_is_compatible(faa->fa_node, "ti,omap4-gpio")) {
		sc->sc_padconf_set_gpioflags = NULL;
		sc->sc_regs.revision = GPIO4_REVISION;
		sc->sc_regs.sysconfig = GPIO4_SYSCONFIG;
		sc->sc_regs.irqstatus_raw0 = GPIO4_IRQSTATUS_RAW_0;
		sc->sc_regs.irqstatus_raw1 = GPIO4_IRQSTATUS_RAW_1;
		sc->sc_regs.irqstatus0 = GPIO4_IRQSTATUS_0;
		sc->sc_regs.irqstatus1 = GPIO4_IRQSTATUS_1;
		sc->sc_regs.irqstatus_set0 = GPIO4_IRQSTATUS_SET_0;
		sc->sc_regs.irqstatus_set1 = GPIO4_IRQSTATUS_SET_1;
		sc->sc_regs.irqstatus_clear0 = GPIO4_IRQSTATUS_CLR_0;
		sc->sc_regs.irqstatus_clear1 = GPIO4_IRQSTATUS_CLR_1;
		sc->sc_regs.irqwaken0 = GPIO4_IRQWAKEN_0;
		sc->sc_regs.irqwaken1 = GPIO4_IRQWAKEN_1;
		sc->sc_regs.sysstatus = GPIO4_SYSSTATUS;
		sc->sc_regs.wakeupenable = -1;
		sc->sc_regs.ctrl = GPIO4_CTRL;
		sc->sc_regs.oe = GPIO4_OE;
		sc->sc_regs.datain = GPIO4_DATAIN;
		sc->sc_regs.dataout = GPIO4_DATAOUT;
		sc->sc_regs.leveldetect0 = GPIO4_LEVELDETECT0;
		sc->sc_regs.leveldetect1 = GPIO4_LEVELDETECT1;
		sc->sc_regs.risingdetect = GPIO4_RISINGDETECT;
		sc->sc_regs.fallingdetect = GPIO4_FALLINGDETECT;
		sc->sc_regs.debounceenable = GPIO4_DEBOUNCENABLE;
		sc->sc_regs.debouncingtime = GPIO4_DEBOUNCINGTIME;
		sc->sc_regs.clearwkupena = -1;
		sc->sc_regs.setwkupena = -1;
		sc->sc_regs.cleardataout = GPIO4_CLEARDATAOUT;
		sc->sc_regs.setdataout = GPIO4_SETDATAOUT;
	} else
		panic("%s: could not find a compatible soc",
		    sc->sc_dev.dv_xname);

	rev = READ4(sc, sc->sc_regs.revision);

	printf(": rev %d.%d\n", rev >> 4 & 0xf, rev & 0xf);

	WRITE4(sc, sc->sc_regs.irqstatus_clear0, ~0);
	WRITE4(sc, sc->sc_regs.irqstatus_clear1, ~0);

	/* XXX - SYSCONFIG */
	/* XXX - CTRL */
	/* XXX - DEBOUNCE */

	for (i = 0; i < GPIO_NUM_PINS; i++) {
		sc->sc_gpio_pins[i].pin_num = i;
		sc->sc_gpio_pins[i].pin_caps = GPIO_PIN_INPUT |
		    GPIO_PIN_OUTPUT | GPIO_PIN_PULLUP | GPIO_PIN_PULLDOWN;
		sc->sc_gpio_pins[i].pin_state = omgpio_pin_read(sc, i) ?
		    GPIO_PIN_HIGH : GPIO_PIN_LOW;
		sc->sc_gpio_pins[i].pin_flags = GPIO_PIN_SET;
	}

	sc->sc_gc.gc_node = sc->sc_node;
	sc->sc_gc.gc_cookie = sc;
	sc->sc_gc.gc_config_pin = omgpio_config_pin;
	sc->sc_gc.gc_get_pin = omgpio_get_pin;
	sc->sc_gc.gc_set_pin = omgpio_set_pin;
	gpio_controller_register(&sc->sc_gc);

	sc->sc_gpio_gc.gp_cookie = sc;
	sc->sc_gpio_gc.gp_pin_read = omgpio_pin_read;
	sc->sc_gpio_gc.gp_pin_write = omgpio_pin_write;
	sc->sc_gpio_gc.gp_pin_ctl = omgpio_pin_ctl;

	gba.gba_name = "gpio";
	gba.gba_gc = &sc->sc_gpio_gc;
	gba.gba_pins = sc->sc_gpio_pins;
	gba.gba_npins = GPIO_NUM_PINS;

#if NGPIO > 0
	config_found(&sc->sc_dev, &gba, gpiobus_print);
#endif
}

/* XXX - This assumes MCU INTERRUPTS are IRQ1, and DSP are IRQ2 */

#if 0
/* XXX - FIND THESE REGISTERS !!! */
unsigned int
omgpio_get_function(unsigned int gpio, unsigned int fn)
{
	return 0;
}

void
omgpio_set_function(unsigned int gpio, unsigned int fn)
{
}
#endif

void
omgpio_config_pin(void *cookie, uint32_t *cells, int config)
{
	struct omgpio_softc *sc = cookie;
	uint32_t pin = cells[0];

	if (pin >= GPIO_NUM_PINS)
		return;

	if (config & GPIO_CONFIG_OUTPUT)
		omgpio_pin_dir_write(sc, pin, OMGPIO_DIR_OUT);
	else
		omgpio_pin_dir_write(sc, pin, OMGPIO_DIR_IN);
}

int
omgpio_get_pin(void *cookie, uint32_t *cells)
{
	struct omgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];
	int val;

	if (pin >= GPIO_NUM_PINS)
		return 0;

	val = omgpio_pin_read(sc, pin);
	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	return val;
}

void
omgpio_set_pin(void *cookie, uint32_t *cells, int val)
{
	struct omgpio_softc *sc = cookie;
	uint32_t pin = cells[0];
	uint32_t flags = cells[1];

	if (pin >= GPIO_NUM_PINS)
		return;

	if (flags & GPIO_ACTIVE_LOW)
		val = !val;
	omgpio_pin_write(sc, pin, val);
}

unsigned int
omgpio_get_bit(unsigned int gpio)
{
	struct omgpio_softc *sc = omgpio_cd.cd_devs[GPIO_PIN_TO_INST(gpio)];

	return omgpio_pin_read(sc, GPIO_PIN_TO_OFFSET(gpio));
}

void
omgpio_set_bit(unsigned int gpio)
{
	struct omgpio_softc *sc = omgpio_cd.cd_devs[GPIO_PIN_TO_INST(gpio)];

	omgpio_pin_write(sc, GPIO_PIN_TO_OFFSET(gpio), GPIO_PIN_HIGH);
}

void
omgpio_clear_bit(unsigned int gpio)
{
	struct omgpio_softc *sc = omgpio_cd.cd_devs[GPIO_PIN_TO_INST(gpio)];

	omgpio_pin_write(sc, GPIO_PIN_TO_OFFSET(gpio), GPIO_PIN_LOW);
}

void
omgpio_set_dir(unsigned int gpio, unsigned int dir)
{
	struct omgpio_softc *sc = omgpio_cd.cd_devs[GPIO_PIN_TO_INST(gpio)];

	omgpio_pin_dir_write(sc, GPIO_PIN_TO_OFFSET(gpio), dir);
}

int
omgpio_pin_read(void *arg, int pin)
{
	struct omgpio_softc *sc = arg;
	u_int32_t reg;

	if(omgpio_pin_dir_read(sc, pin) == OMGPIO_DIR_IN)
		reg = READ4(sc, sc->sc_regs.datain);
	else
		reg = READ4(sc, sc->sc_regs.dataout);
	return (reg >> GPIO_PIN_TO_OFFSET(pin)) & 0x1;
}

void
omgpio_pin_write(void *arg, int pin, int value)
{
	struct omgpio_softc *sc = arg;

	if (value)
		WRITE4(sc, sc->sc_regs.setdataout,
		    1 << GPIO_PIN_TO_OFFSET(pin));
	else
		WRITE4(sc, sc->sc_regs.cleardataout,
		    1 << GPIO_PIN_TO_OFFSET(pin));
}

void
omgpio_pin_ctl(void *arg, int pin, int flags)
{
	struct omgpio_softc *sc = arg;

	if (flags & GPIO_PIN_INPUT)
		omgpio_pin_dir_write(sc, pin, OMGPIO_DIR_IN);
	else if (flags & GPIO_PIN_OUTPUT)
		omgpio_pin_dir_write(sc, pin, OMGPIO_DIR_OUT);

	if (sc->sc_padconf_set_gpioflags)
		sc->sc_padconf_set_gpioflags(
		    sc->sc_dev.dv_unit * GPIO_NUM_PINS + pin, flags);
}

void
omgpio_pin_dir_write(struct omgpio_softc *sc, unsigned int gpio,
    unsigned int dir)
{
	int s;
	u_int32_t reg;

	s = splhigh();

	reg = READ4(sc, sc->sc_regs.oe);
	if (dir == OMGPIO_DIR_IN)
		reg |= 1 << GPIO_PIN_TO_OFFSET(gpio);
	else
		reg &= ~(1 << GPIO_PIN_TO_OFFSET(gpio));
	WRITE4(sc, sc->sc_regs.oe, reg);

	splx(s);
}

int
omgpio_pin_dir_read(struct omgpio_softc *sc, unsigned int gpio)
{
	u_int32_t reg;
	reg = READ4(sc, sc->sc_regs.oe);
	if (reg & (1 << GPIO_PIN_TO_OFFSET(gpio)))
		return OMGPIO_DIR_IN;
	else
		return OMGPIO_DIR_OUT;
}

#if 0
void
omgpio_clear_intr(struct omgpio_softc *sc, unsigned int gpio)
{
	struct omgpio_softc *sc = omgpio_cd.cd_devs[GPIO_PIN_TO_INST(gpio)];

	WRITE4(sc, sc->sc_regs.irqstatus0, 1 << GPIO_PIN_TO_OFFSET(gpio));
}

void
omgpio_intr_mask(struct omgpio_softc *sc, unsigned int gpio)
{
	struct omgpio_softc *sc = omgpio_cd.cd_devs[GPIO_PIN_TO_INST(gpio)];

	WRITE4(sc, sc->sc_regs.irqstatus_clear0, 1 << GPIO_PIN_TO_OFFSET(gpio));
}

void
omgpio_intr_unmask(struct omgpio_softc *sc, unsigned int gpio)
{
	struct omgpio_softc *sc = omgpio_cd.cd_devs[GPIO_PIN_TO_INST(gpio)];

	WRITE4(sc, sc->sc_regs.irqstatus_set0, 1 << GPIO_PIN_TO_OFFSET(gpio));
}

void
omgpio_intr_level(struct omgpio_softc *sc, unsigned int gpio, unsigned int level)
{
	u_int32_t fe, re, l0, l1, bit;
	struct omgpio_softc *sc = omgpio_cd.cd_devs[GPIO_PIN_TO_INST(gpio)];
	int s;

	s = splhigh();

	fe = READ4(sc, sc->sc_regs.fallingdetect);
	re = READ4(sc, sc->sc_regs.risingdetect);
	l0 = READ4(sc, sc->sc_regs.leveldetect0);
	l1 = READ4(sc, sc->sc_regs.leveldetect1);

	bit = 1 << GPIO_PIN_TO_OFFSET(gpio);

	switch (level) {
	case IST_NONE:
		fe &= ~bit;
		re &= ~bit;
		l0 &= ~bit;
		l1 &= ~bit;
		break;
	case IST_EDGE_FALLING:
		fe |= bit;
		re &= ~bit;
		l0 &= ~bit;
		l1 &= ~bit;
		break;
	case IST_EDGE_RISING:
		fe &= ~bit;
		re |= bit;
		l0 &= ~bit;
		l1 &= ~bit;
		break;
	case IST_PULSE: /* XXX */
		/* FALLTHRU */
	case IST_EDGE_BOTH:
		fe |= bit;
		re |= bit;
		l0 &= ~bit;
		l1 &= ~bit;
		break;
	case IST_LEVEL_LOW:
		fe &= ~bit;
		re &= ~bit;
		l0 |= bit;
		l1 &= ~bit;
		break;
	case IST_LEVEL_HIGH:
		fe &= ~bit;
		re &= ~bit;
		l0 &= ~bit;
		l1 |= bit;
		break;
	default:
		panic("omgpio_intr_level: bad level: %d", level);
		break;
	}

	WRITE4(sc, sc->sc_regs.fallingdetect, fe);
	WRITE4(sc, sc->sc_regs.risingdetect, re);
	WRITE4(sc, sc->sc_regs.leveldetect0, l0);
	WRITE4(sc, sc->sc_regs.leveldetect1, l1);

	splx(s);
}

void *
omgpio_intr_establish(struct omgpio_softc *sc, unsigned int gpio, int level, int spl,
    int (*func)(void *), void *arg, char *name)
{
	int psw;
	struct intrhand *ih;
	struct omgpio_softc *sc;

	/*
	 * XXX - is gpio here the pin or the interrupt number
	 * which is 96 + gpio pin?
	 */

	if (GPIO_PIN_TO_INST(gpio) > omgpio_cd.cd_ndevs)
		panic("omgpio_intr_establish: bogus irqnumber %d: %s",
		    gpio, name);

	sc = omgpio_cd.cd_devs[GPIO_PIN_TO_INST(gpio)];

	if (sc->sc_handlers[GPIO_PIN_TO_OFFSET(gpio)] != NULL)
		panic("omgpio_intr_establish: gpio pin busy %d old %s new %s",
		    gpio, sc->sc_handlers[GPIO_PIN_TO_OFFSET(gpio)]->ih_name,
		    name);

	psw = disable_interrupts(PSR_I);

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = level & IPL_IRQMASK;
	ih->ih_gpio = gpio;
	ih->ih_irq = gpio + 512;
	ih->ih_name = name;

	sc->sc_handlers[GPIO_PIN_TO_OFFSET(gpio)] = ih;

	evcount_attach(&ih->ih_count, name, &ih->ih_irq);

	omgpio_intr_level(gpio, level);
	omgpio_intr_unmask(gpio);

	omgpio_recalc_interrupts(sc);

	restore_interrupts(psw);

	return (ih);
}

void
omgpio_intr_disestablish(struct omgpio_softc *sc, void *cookie)
{
	int psw;
	struct intrhand *ih = cookie;
	struct omgpio_softc *sc = omgpio_cd.cd_devs[GPIO_PIN_TO_INST(ih->ih_gpio)];
	int gpio = ih->ih_gpio;
	psw = disable_interrupts(PSR_I);

	ih = sc->sc_handlers[GPIO_PIN_TO_OFFSET(gpio)];
	sc->sc_handlers[GPIO_PIN_TO_OFFSET(gpio)] = NULL;

	evcount_detach(&ih->ih_count);

	free(ih, M_DEVBUF, 0);

	omgpio_intr_level(gpio, IST_NONE);
	omgpio_intr_mask(gpio);
	omgpio_clear_intr(gpio); /* Just in case */

	omgpio_recalc_interrupts(sc);

	restore_interrupts(psw);
}

int
omgpio_irq(void *v)
{
	struct omgpio_softc *sc = v;
	u_int32_t pending;
	struct intrhand *ih;
	int bit;

	pending = READ4(sc, omgpio.irqstatus0);

	while (pending != 0) {
		bit = ffs(pending) - 1;
		ih = sc->sc_handlers[bit];

		if (ih != NULL) {
			if (ih->ih_func(ih->ih_arg))
				ih->ih_count.ec_count++;
			omgpio_clear_intr(ih->ih_gpio);
		} else {
			panic("omgpio: irq fired no handler, gpio %x %x %x",
				sc->sc_dev.dv_unit * 32 + bit, pending,
	READ4(sc, omgpio.irqstatus0)

				);
		}
		pending &= ~(1 << bit);
	}
	return 1;
}

int
omgpio_irq_dummy(void *v)
{
	return 0;
}

void
omgpio_recalc_interrupts(struct omgpio_softc *sc)
{
	struct intrhand *ih;
	int max = IPL_NONE;
	int min = IPL_HIGH;
	int i;

	for (i = 0; i < GPIO_NUM_PINS; i++) {
		ih = sc->sc_handlers[i];
		if (ih != NULL) {
			if (ih->ih_ipl > max)
				max = ih->ih_ipl;

			if (ih->ih_ipl < min)
				min = ih->ih_ipl;
		}
	}
	if (max == IPL_NONE)
		min = IPL_NONE;

#if 0
	if ((max == IPL_NONE || max != sc->sc_max_il) && sc->sc_ih_h != NULL)
		arm_intr_disestablish_fdt(sc->sc_ih_h);

	if (max != IPL_NONE && max != sc->sc_max_il) {
		sc->sc_ih_h = arm_intr_establish_fdt(sc->sc_node, max, omgpio_irq,
		    sc, NULL);
	}
#else
	if (sc->sc_ih_h != NULL)
		arm_intr_disestablish_fdt(sc->sc_ih_h);

	if (max != IPL_NONE) {
		sc->sc_ih_h = arm_intr_establish_fdt(sc->sc_node, max, omgpio_irq,
		    sc, NULL);
	}
#endif

	sc->sc_max_il = max;

	if (sc->sc_ih_l != NULL)
		arm_intr_disestablish_fdt(sc->sc_ih_l);

	if (max != min) {
		sc->sc_ih_h = arm_intr_establish_fdt(sc->sc_node, min,
		    omgpio_irq_dummy, sc, NULL);
	}
	sc->sc_min_il = min;
}
#endif
