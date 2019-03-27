/*-
 * Copyright (c) 2016 Daniel Wyatt <Daniel.Wyatt@gmail.com>
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
 * Nuvoton GPIO driver.
 *
 */

#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/eventhandler.h>
#include <sys/lock.h>

#include <sys/module.h>
#include <sys/rman.h>
#include <sys/gpio.h>

#include <isa/isavar.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/gpio/gpiobusvar.h>

#include "gpio_if.h"

/*
 * Global configuration registers (CR).
 */
#define NCT_CR_LDN			0x07	/* Logical Device Number */
#define NCT_CR_CHIP_ID			0x20 	/* Chip ID */
#define NCT_CR_CHIP_ID_H		0x20 	/* Chip ID (high byte) */
#define NCT_CR_CHIP_ID_L		0x21 	/* Chip ID (low byte) */
#define NCT_CR_OPT_1			0x26	/* Global Options (1) */

/* Logical Device Numbers. */
#define NCT_LDN_GPIO			0x07
#define NCT_LDN_GPIO_CFG		0x08
#define NCT_LDN_GPIO_MODE		0x0f

/* Logical Device 7 */
#define NCT_LD7_GPIO_ENABLE		0x30
#define NCT_LD7_GPIO0_IOR		0xe0
#define NCT_LD7_GPIO0_DAT		0xe1
#define NCT_LD7_GPIO0_INV		0xe2
#define NCT_LD7_GPIO0_DST		0xe3
#define NCT_LD7_GPIO1_IOR		0xe4
#define NCT_LD7_GPIO1_DAT		0xe5
#define NCT_LD7_GPIO1_INV		0xe6
#define NCT_LD7_GPIO1_DST		0xe7

/* Logical Device F */
#define NCT_LDF_GPIO0_OUTCFG		0xe0
#define NCT_LDF_GPIO1_OUTCFG		0xe1

#define NCT_EXTFUNC_ENTER		0x87
#define NCT_EXTFUNC_EXIT		0xaa

#define NCT_MAX_PIN			15
#define NCT_IS_VALID_PIN(_p)	((_p) >= 0 && (_p) <= NCT_MAX_PIN)

#define NCT_PIN_BIT(_p)         (1 << ((_p) % 8))

#define NCT_GPIO_CAPS	(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | \
	GPIO_PIN_OPENDRAIN | GPIO_PIN_PUSHPULL | \
	GPIO_PIN_INVIN | GPIO_PIN_INVOUT)

struct nct_softc {
	device_t			dev;
	device_t			busdev;
	struct mtx			mtx;
	struct resource			*portres;
	int				rid;
	struct gpio_pin			pins[NCT_MAX_PIN + 1];
};

#define GPIO_LOCK_INIT(_sc)	mtx_init(&(_sc)->mtx,		\
		device_get_nameunit(dev), NULL, MTX_DEF)
#define GPIO_LOCK_DESTROY(_sc)		mtx_destroy(&(_sc)->mtx)
#define GPIO_LOCK(_sc)		mtx_lock(&(_sc)->mtx)
#define GPIO_UNLOCK(_sc)	mtx_unlock(&(_sc)->mtx)
#define GPIO_ASSERT_LOCKED(_sc)	mtx_assert(&(_sc)->mtx, MA_OWNED)
#define GPIO_ASSERT_UNLOCKED(_sc)	mtx_assert(&(_sc)->mtx, MA_NOTOWNED)

#define NCT_BARRIER_WRITE(_sc)	\
	bus_barrier((_sc)->portres, 0, 2, BUS_SPACE_BARRIER_WRITE)

#define NCT_BARRIER_READ_WRITE(_sc)	\
	bus_barrier((_sc)->portres, 0, 2, \
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE)

static void	ext_cfg_enter(struct nct_softc *);
static void	ext_cfg_exit(struct nct_softc *);

/*
 * Potential Extended Function Enable Register addresses.
 * Same address as EFIR.
 */
uint8_t probe_addrs[] = {0x2e, 0x4e};

struct nuvoton_vendor_device_id {
	uint16_t		chip_id;
	const char *		descr;
} nct_devs[] = {
	{
		.chip_id	= 0x1061,
		.descr		= "Nuvoton NCT5104D",
	},
	{
		.chip_id	= 0xc452,
		.descr		= "Nuvoton NCT5104D (PC-Engines APU)",
	},
	{
		.chip_id	= 0xc453,
		.descr		= "Nuvoton NCT5104D (PC-Engines APU3)",
	},
};

static void
write_cfg_reg_1(struct nct_softc *sc, uint8_t reg, uint8_t value)
{
	GPIO_ASSERT_LOCKED(sc);
	bus_write_1(sc->portres, 0, reg);
	NCT_BARRIER_WRITE(sc);
	bus_write_1(sc->portres, 1, value);
	NCT_BARRIER_WRITE(sc);
}

static uint8_t
read_cfg_reg_1(struct nct_softc *sc, uint8_t reg)
{
	uint8_t value;

	GPIO_ASSERT_LOCKED(sc);
	bus_write_1(sc->portres, 0, reg);
	NCT_BARRIER_READ_WRITE(sc);
	value = bus_read_1(sc->portres, 1);
	NCT_BARRIER_READ_WRITE(sc);
	
	return (value);
}

static uint16_t
read_cfg_reg_2(struct nct_softc *sc, uint8_t reg)
{
	uint16_t value;

	value = read_cfg_reg_1(sc, reg) << 8;
	value |= read_cfg_reg_1(sc, reg + 1);

	return (value);
}

/*
 * Enable extended function mode.
 *
 */
static void
ext_cfg_enter(struct nct_softc *sc)
{
	GPIO_ASSERT_LOCKED(sc);
	bus_write_1(sc->portres, 0, NCT_EXTFUNC_ENTER);
	NCT_BARRIER_WRITE(sc);
	bus_write_1(sc->portres, 0, NCT_EXTFUNC_ENTER);
	NCT_BARRIER_WRITE(sc);
}

/*
 * Disable extended function mode.
 *
 */
static void
ext_cfg_exit(struct nct_softc *sc)
{
	GPIO_ASSERT_LOCKED(sc);
	bus_write_1(sc->portres, 0, NCT_EXTFUNC_EXIT);
	NCT_BARRIER_WRITE(sc);
}

/*
 * Select a Logical Device.
 */
static void
select_ldn(struct nct_softc *sc, uint8_t ldn)
{
	write_cfg_reg_1(sc, NCT_CR_LDN, ldn);
}

/*
 * Get the GPIO Input/Output register address
 * for a pin.
 */
static uint8_t
nct_ior_addr(uint32_t pin_num)
{
	uint8_t addr;

	addr = NCT_LD7_GPIO0_IOR;
	if (pin_num > 7)
		addr = NCT_LD7_GPIO1_IOR;

	return (addr);
}

/*
 * Get the GPIO Data register address for a pin.
 */
static uint8_t
nct_dat_addr(uint32_t pin_num)
{
	uint8_t addr;

	addr = NCT_LD7_GPIO0_DAT;
	if (pin_num > 7)
		addr = NCT_LD7_GPIO1_DAT;

	return (addr);
}

/*
 * Get the GPIO Inversion register address
 * for a pin.
 */
static uint8_t
nct_inv_addr(uint32_t pin_num)
{
	uint8_t addr;

	addr = NCT_LD7_GPIO0_INV;
	if (pin_num > 7)
		addr = NCT_LD7_GPIO1_INV;

	return (addr);
}

/*
 * Get the GPIO Output Configuration/Mode
 * register address for a pin.
 */
static uint8_t
nct_outcfg_addr(uint32_t pin_num)
{
	uint8_t addr;

	addr = NCT_LDF_GPIO0_OUTCFG;
	if (pin_num > 7)
		addr = NCT_LDF_GPIO1_OUTCFG;

	return (addr);
}

/*
 * Set a pin to output mode.
 */
static void
nct_set_pin_is_output(struct nct_softc *sc, uint32_t pin_num)
{
	uint8_t reg;
	uint8_t ior;

	reg = nct_ior_addr(pin_num);
	select_ldn(sc, NCT_LDN_GPIO);
	ior = read_cfg_reg_1(sc, reg);
	ior &= ~(NCT_PIN_BIT(pin_num));
	write_cfg_reg_1(sc, reg, ior);
}

/*
 * Set a pin to input mode.
 */
static void
nct_set_pin_is_input(struct nct_softc *sc, uint32_t pin_num)
{
	uint8_t reg;
	uint8_t ior;

	reg = nct_ior_addr(pin_num);
	select_ldn(sc, NCT_LDN_GPIO);
	ior = read_cfg_reg_1(sc, reg);
	ior |= NCT_PIN_BIT(pin_num);
	write_cfg_reg_1(sc, reg, ior);
}

/*
 * Check whether a pin is configured as an input.
 */
static bool
nct_pin_is_input(struct nct_softc *sc, uint32_t pin_num)
{
	uint8_t reg;
	uint8_t ior;

	reg = nct_ior_addr(pin_num);
	select_ldn(sc, NCT_LDN_GPIO);
	ior = read_cfg_reg_1(sc, reg);

	return (ior & NCT_PIN_BIT(pin_num));
}

/*
 * Write a value to an output pin.
 */
static void
nct_write_pin(struct nct_softc *sc, uint32_t pin_num, uint8_t data)
{
	uint8_t reg;
	uint8_t value;

	reg = nct_dat_addr(pin_num);
	select_ldn(sc, NCT_LDN_GPIO);
	value = read_cfg_reg_1(sc, reg);
	if (data)
		value |= NCT_PIN_BIT(pin_num);
	else
		value &= ~(NCT_PIN_BIT(pin_num));

	write_cfg_reg_1(sc, reg, value);
}

static bool
nct_read_pin(struct nct_softc *sc, uint32_t pin_num)
{
	uint8_t reg;

	reg = nct_dat_addr(pin_num);
	select_ldn(sc, NCT_LDN_GPIO);

	return (read_cfg_reg_1(sc, reg) & NCT_PIN_BIT(pin_num));
}

static void
nct_set_pin_is_inverted(struct nct_softc *sc, uint32_t pin_num)
{
	uint8_t reg;
	uint8_t inv;

	reg = nct_inv_addr(pin_num);
	select_ldn(sc, NCT_LDN_GPIO);
	inv = read_cfg_reg_1(sc, reg);
	inv |= (NCT_PIN_BIT(pin_num));
	write_cfg_reg_1(sc, reg, inv);
}

static void
nct_set_pin_not_inverted(struct nct_softc *sc, uint32_t pin_num)
{
	uint8_t reg;
	uint8_t inv;

	reg = nct_inv_addr(pin_num);
	select_ldn(sc, NCT_LDN_GPIO);
	inv = read_cfg_reg_1(sc, reg);
	inv &= ~(NCT_PIN_BIT(pin_num));
	write_cfg_reg_1(sc, reg, inv);
}

static bool
nct_pin_is_inverted(struct nct_softc *sc, uint32_t pin_num)
{
	uint8_t reg;
	uint8_t inv;

	reg = nct_inv_addr(pin_num);
	select_ldn(sc, NCT_LDN_GPIO);
	inv = read_cfg_reg_1(sc, reg);

	return (inv & NCT_PIN_BIT(pin_num));
}

static void
nct_set_pin_opendrain(struct nct_softc *sc, uint32_t pin_num)
{
	uint8_t reg;
	uint8_t outcfg;

	reg = nct_outcfg_addr(pin_num);
	select_ldn(sc, NCT_LDN_GPIO_MODE);
	outcfg = read_cfg_reg_1(sc, reg);
	outcfg |= (NCT_PIN_BIT(pin_num));
	write_cfg_reg_1(sc, reg, outcfg);
}

static void
nct_set_pin_pushpull(struct nct_softc *sc, uint32_t pin_num)
{
	uint8_t reg;
	uint8_t outcfg;

	reg = nct_outcfg_addr(pin_num);
	select_ldn(sc, NCT_LDN_GPIO_MODE);
	outcfg = read_cfg_reg_1(sc, reg);
	outcfg &= ~(NCT_PIN_BIT(pin_num));
	write_cfg_reg_1(sc, reg, outcfg);
}

static bool
nct_pin_is_opendrain(struct nct_softc *sc, uint32_t pin_num)
{
	uint8_t reg;
	uint8_t outcfg;

	reg = nct_outcfg_addr(pin_num);
	select_ldn(sc, NCT_LDN_GPIO_MODE);
	outcfg = read_cfg_reg_1(sc, reg);

	return (outcfg & NCT_PIN_BIT(pin_num));
}

static void
nct_identify(driver_t *driver, device_t parent)
{
	if (device_find_child(parent, driver->name, 0) != NULL)
		return;

	BUS_ADD_CHILD(parent, 0, driver->name, 0);
}

static int
nct_probe(device_t dev)
{
	int i, j;
	int rc;
	struct nct_softc *sc;
	uint16_t chipid;

	/* Make sure we do not claim some ISA PNP device. */
	if (isa_get_logicalid(dev) != 0)
		return (ENXIO);

	sc = device_get_softc(dev);

	for (i = 0; i < nitems(probe_addrs); i++) {
		sc->rid = 0;
		sc->portres = bus_alloc_resource(dev, SYS_RES_IOPORT, &sc->rid,
			probe_addrs[i], probe_addrs[i] + 1, 2, RF_ACTIVE);
		if (sc->portres == NULL)
			continue;

		GPIO_LOCK_INIT(sc);

		GPIO_ASSERT_UNLOCKED(sc);
		GPIO_LOCK(sc);
		ext_cfg_enter(sc);
		chipid = read_cfg_reg_2(sc, NCT_CR_CHIP_ID);
		ext_cfg_exit(sc);
		GPIO_UNLOCK(sc);

		GPIO_LOCK_DESTROY(sc);

		bus_release_resource(dev, SYS_RES_IOPORT, sc->rid, sc->portres);
		bus_delete_resource(dev, SYS_RES_IOPORT, sc->rid);

		for (j = 0; j < nitems(nct_devs); j++) {
			if (chipid == nct_devs[j].chip_id) {
				rc = bus_set_resource(dev, SYS_RES_IOPORT, 0, probe_addrs[i], 2);
				if (rc != 0) {
					device_printf(dev, "bus_set_resource failed for address 0x%02X\n", probe_addrs[i]);
					continue;
				}
				device_set_desc(dev, nct_devs[j].descr);
				return (BUS_PROBE_DEFAULT);
			}
		}
	}
	return (ENXIO);
}

static int
nct_attach(device_t dev)
{
	struct nct_softc *sc;
	int i;

	sc = device_get_softc(dev);

	sc->rid = 0;
	sc->portres = bus_alloc_resource(dev, SYS_RES_IOPORT, &sc->rid,
		0ul, ~0ul, 2, RF_ACTIVE);
	if (sc->portres == NULL) {
		device_printf(dev, "cannot allocate ioport\n");
		return (ENXIO);
	}

	GPIO_LOCK_INIT(sc);

	GPIO_ASSERT_UNLOCKED(sc);
	GPIO_LOCK(sc);
	ext_cfg_enter(sc);
	select_ldn(sc, NCT_LDN_GPIO);
	/* Enable gpio0 and gpio1. */
	write_cfg_reg_1(sc, NCT_LD7_GPIO_ENABLE,
		read_cfg_reg_1(sc, NCT_LD7_GPIO_ENABLE) | 0x03);

	for (i = 0; i <= NCT_MAX_PIN; i++) {
		struct gpio_pin *pin;

		pin = &sc->pins[i];
		pin->gp_pin = i;
		pin->gp_caps = NCT_GPIO_CAPS;
		pin->gp_flags = 0;

		snprintf(pin->gp_name, GPIOMAXNAME, "GPIO%02u", i);
		pin->gp_name[GPIOMAXNAME - 1] = '\0';

		if (nct_pin_is_input(sc, i))
			pin->gp_flags |= GPIO_PIN_INPUT;
		else
			pin->gp_flags |= GPIO_PIN_OUTPUT;

		if (nct_pin_is_opendrain(sc, i))
			pin->gp_flags |= GPIO_PIN_OPENDRAIN;
		else
			pin->gp_flags |= GPIO_PIN_PUSHPULL;

		if (nct_pin_is_inverted(sc, i))
			pin->gp_flags |= (GPIO_PIN_INVIN | GPIO_PIN_INVOUT);
	}
	GPIO_UNLOCK(sc);

	sc->busdev = gpiobus_attach_bus(dev);
	if (sc->busdev == NULL) {
		GPIO_ASSERT_UNLOCKED(sc);
		GPIO_LOCK(sc);
		ext_cfg_exit(sc);
		GPIO_UNLOCK(sc);
		bus_release_resource(dev, SYS_RES_IOPORT, sc->rid, sc->portres);
		GPIO_LOCK_DESTROY(sc);

		return (ENXIO);
	}

	return (0);
}

static int
nct_detach(device_t dev)
{
	struct nct_softc *sc;

	sc = device_get_softc(dev);
	gpiobus_detach_bus(dev);

	GPIO_ASSERT_UNLOCKED(sc);
	GPIO_LOCK(sc);
	ext_cfg_exit(sc);
	GPIO_UNLOCK(sc);

	/* Cleanup resources. */
	bus_release_resource(dev, SYS_RES_IOPORT, sc->rid, sc->portres);

	GPIO_LOCK_DESTROY(sc);

	return (0);
}

static device_t
nct_gpio_get_bus(device_t dev)
{
	struct nct_softc *sc;

	sc = device_get_softc(dev);

	return (sc->busdev);
}

static int
nct_gpio_pin_max(device_t dev, int *npins)
{
	*npins = NCT_MAX_PIN;

	return (0);
}

static int
nct_gpio_pin_set(device_t dev, uint32_t pin_num, uint32_t pin_value)
{
	struct nct_softc *sc;

	if (!NCT_IS_VALID_PIN(pin_num))
		return (EINVAL);

	sc = device_get_softc(dev);
	GPIO_ASSERT_UNLOCKED(sc);
	GPIO_LOCK(sc);
	nct_write_pin(sc, pin_num, pin_value);
	GPIO_UNLOCK(sc);

	return (0);
}

static int
nct_gpio_pin_get(device_t dev, uint32_t pin_num, uint32_t *pin_value)
{
	struct nct_softc *sc;

	if (!NCT_IS_VALID_PIN(pin_num))
		return (EINVAL);

	sc = device_get_softc(dev);
	GPIO_ASSERT_UNLOCKED(sc);
	GPIO_LOCK(sc);
	*pin_value = nct_read_pin(sc, pin_num);
	GPIO_UNLOCK(sc);

	return (0);
}

static int
nct_gpio_pin_toggle(device_t dev, uint32_t pin_num)
{
	struct nct_softc *sc;

	if (!NCT_IS_VALID_PIN(pin_num))
		return (EINVAL);

	sc = device_get_softc(dev);
	GPIO_ASSERT_UNLOCKED(sc);
	GPIO_LOCK(sc);
	if (nct_read_pin(sc, pin_num))
		nct_write_pin(sc, pin_num, 0);
	else
		nct_write_pin(sc, pin_num, 1);

	GPIO_UNLOCK(sc);

	return (0);
}

static int
nct_gpio_pin_getcaps(device_t dev, uint32_t pin_num, uint32_t *caps)
{
	struct nct_softc *sc;

	if (!NCT_IS_VALID_PIN(pin_num))
		return (EINVAL);

	sc = device_get_softc(dev);
	GPIO_ASSERT_UNLOCKED(sc);
	GPIO_LOCK(sc);
	*caps = sc->pins[pin_num].gp_caps;
	GPIO_UNLOCK(sc);

	return (0);
}

static int
nct_gpio_pin_getflags(device_t dev, uint32_t pin_num, uint32_t *flags)
{
	struct nct_softc *sc;

	if (!NCT_IS_VALID_PIN(pin_num))
		return (EINVAL);

	sc = device_get_softc(dev);
	GPIO_ASSERT_UNLOCKED(sc);
	GPIO_LOCK(sc);
	*flags = sc->pins[pin_num].gp_flags;
	GPIO_UNLOCK(sc);

	return (0);
}

static int
nct_gpio_pin_getname(device_t dev, uint32_t pin_num, char *name)
{
	struct nct_softc *sc;

	if (!NCT_IS_VALID_PIN(pin_num))
		return (EINVAL);

	sc = device_get_softc(dev);
	GPIO_ASSERT_UNLOCKED(sc);
	GPIO_LOCK(sc);
	memcpy(name, sc->pins[pin_num].gp_name, GPIOMAXNAME);
	GPIO_UNLOCK(sc);

	return (0);
}

static int
nct_gpio_pin_setflags(device_t dev, uint32_t pin_num, uint32_t flags)
{
	struct nct_softc *sc;
	struct gpio_pin *pin;

	if (!NCT_IS_VALID_PIN(pin_num))
		return (EINVAL);

	sc = device_get_softc(dev);
	pin = &sc->pins[pin_num];
	if ((flags & pin->gp_caps) != flags)
		return (EINVAL);

	GPIO_ASSERT_UNLOCKED(sc);
	GPIO_LOCK(sc);
	if (flags & (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)) {
		if ((flags & (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)) ==
			(GPIO_PIN_INPUT | GPIO_PIN_OUTPUT)) {
				GPIO_UNLOCK(sc);
				return (EINVAL);
		}

		if (flags & GPIO_PIN_INPUT)
			nct_set_pin_is_input(sc, pin_num);
		else
			nct_set_pin_is_output(sc, pin_num);
	}

	if (flags & (GPIO_PIN_OPENDRAIN | GPIO_PIN_PUSHPULL)) {
		if (flags & GPIO_PIN_INPUT) {
			GPIO_UNLOCK(sc);
			return (EINVAL);
		}

		if ((flags & (GPIO_PIN_OPENDRAIN | GPIO_PIN_PUSHPULL)) ==
			(GPIO_PIN_OPENDRAIN | GPIO_PIN_PUSHPULL)) {
				GPIO_UNLOCK(sc);
				return (EINVAL);
		}

		if (flags & GPIO_PIN_OPENDRAIN)
			nct_set_pin_opendrain(sc, pin_num);
		else
			nct_set_pin_pushpull(sc, pin_num);
	}

	if (flags & (GPIO_PIN_INVIN | GPIO_PIN_INVOUT)) {
		if ((flags & (GPIO_PIN_INVIN | GPIO_PIN_INVOUT)) !=
			(GPIO_PIN_INVIN | GPIO_PIN_INVOUT)) {
				GPIO_UNLOCK(sc);
				return (EINVAL);
		}

		if (flags & GPIO_PIN_INVIN)
			nct_set_pin_is_inverted(sc, pin_num);
		else
			nct_set_pin_not_inverted(sc, pin_num);
	}

	pin->gp_flags = flags;
	GPIO_UNLOCK(sc);

	return (0);
}

static device_method_t nct_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	nct_identify),
	DEVMETHOD(device_probe,		nct_probe),
	DEVMETHOD(device_attach,	nct_attach),
	DEVMETHOD(device_detach,	nct_detach),

	/* GPIO */
	DEVMETHOD(gpio_get_bus,			nct_gpio_get_bus),
	DEVMETHOD(gpio_pin_max,			nct_gpio_pin_max),
	DEVMETHOD(gpio_pin_get,			nct_gpio_pin_get),
	DEVMETHOD(gpio_pin_set,			nct_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle,		nct_gpio_pin_toggle),
	DEVMETHOD(gpio_pin_getname,		nct_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getcaps,		nct_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_getflags,	nct_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_setflags,	nct_gpio_pin_setflags),

	DEVMETHOD_END
};

static driver_t nct_isa_driver = {
	"gpio",
	nct_methods,
	sizeof(struct nct_softc)
};

static devclass_t nct_devclass;

DRIVER_MODULE(nctgpio, isa, nct_isa_driver, nct_devclass, NULL, NULL);
MODULE_DEPEND(nctgpio, gpiobus, 1, 1, 1);
