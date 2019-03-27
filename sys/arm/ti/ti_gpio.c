/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Ben Gray <ben.r.gray@gmail.com>.
 * Copyright (c) 2014 Luiz Otavio O Souza <loos@FreeBSD.org>.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * Beware that the OMAP4 datasheet(s) lists GPIO banks 1-6, whereas the code
 * here uses 0-5.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/gpio.h>
#include <sys/interrupt.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/resource.h>

#include <arm/ti/ti_cpuid.h>
#include <arm/ti/ti_gpio.h>
#include <arm/ti/ti_scm.h>
#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_hwmods.h>

#include <dev/gpio/gpiobusvar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "gpio_if.h"
#include "ti_gpio_if.h"
#include "pic_if.h"

#if !defined(SOC_OMAP4) && !defined(SOC_TI_AM335X)
#error "Unknown SoC"
#endif

/* Register definitions */
#define	TI_GPIO_REVISION		0x0000
#define	TI_GPIO_SYSCONFIG		0x0010
#define	TI_GPIO_IRQSTATUS_RAW_0		0x0024
#define	TI_GPIO_IRQSTATUS_RAW_1		0x0028
#define	TI_GPIO_IRQSTATUS_0		0x002C	/* writing a 0 has no effect */
#define	TI_GPIO_IRQSTATUS_1		0x0030	/* writing a 0 has no effect */
#define	TI_GPIO_IRQSTATUS_SET_0		0x0034	/* writing a 0 has no effect */
#define	TI_GPIO_IRQSTATUS_SET_1		0x0038	/* writing a 0 has no effect */
#define	TI_GPIO_IRQSTATUS_CLR_0		0x003C	/* writing a 0 has no effect */
#define	TI_GPIO_IRQSTATUS_CLR_1		0x0040	/* writing a 0 has no effect */
#define	TI_GPIO_IRQWAKEN_0		0x0044
#define	TI_GPIO_IRQWAKEN_1		0x0048
#define	TI_GPIO_SYSSTATUS		0x0114
#define	TI_GPIO_IRQSTATUS1		0x0118
#define	TI_GPIO_IRQENABLE1		0x011C
#define	TI_GPIO_WAKEUPENABLE		0x0120
#define	TI_GPIO_IRQSTATUS2		0x0128
#define	TI_GPIO_IRQENABLE2		0x012C
#define	TI_GPIO_CTRL			0x0130
#define	TI_GPIO_OE			0x0134
#define	TI_GPIO_DATAIN			0x0138
#define	TI_GPIO_DATAOUT			0x013C
#define	TI_GPIO_LEVELDETECT0		0x0140	/* RW register */
#define	TI_GPIO_LEVELDETECT1		0x0144	/* RW register */
#define	TI_GPIO_RISINGDETECT		0x0148	/* RW register */
#define	TI_GPIO_FALLINGDETECT		0x014C	/* RW register */
#define	TI_GPIO_DEBOUNCENABLE		0x0150
#define	TI_GPIO_DEBOUNCINGTIME		0x0154
#define	TI_GPIO_CLEARWKUPENA		0x0180
#define	TI_GPIO_SETWKUENA		0x0184
#define	TI_GPIO_CLEARDATAOUT		0x0190
#define	TI_GPIO_SETDATAOUT		0x0194

/* Other SoC Specific definitions */
#define	OMAP4_FIRST_GPIO_BANK		1
#define	OMAP4_INTR_PER_BANK		1
#define	OMAP4_GPIO_REV			0x50600801
#define	AM335X_FIRST_GPIO_BANK		0
#define	AM335X_INTR_PER_BANK		2
#define	AM335X_GPIO_REV			0x50600801
#define	PINS_PER_BANK			32
#define	TI_GPIO_MASK(p)			(1U << ((p) % PINS_PER_BANK))

static int ti_gpio_intr(void *arg);
static int ti_gpio_detach(device_t);

static int ti_gpio_pic_attach(struct ti_gpio_softc *sc);
static int ti_gpio_pic_detach(struct ti_gpio_softc *sc);

static u_int
ti_first_gpio_bank(void)
{
	switch(ti_chip()) {
#ifdef SOC_OMAP4
	case CHIP_OMAP_4:
		return (OMAP4_FIRST_GPIO_BANK);
#endif
#ifdef SOC_TI_AM335X
	case CHIP_AM335X:
		return (AM335X_FIRST_GPIO_BANK);
#endif
	}
	return (0);
}

static uint32_t
ti_gpio_rev(void)
{
	switch(ti_chip()) {
#ifdef SOC_OMAP4
	case CHIP_OMAP_4:
		return (OMAP4_GPIO_REV);
#endif
#ifdef SOC_TI_AM335X
	case CHIP_AM335X:
		return (AM335X_GPIO_REV);
#endif
	}
	return (0);
}

/**
 *	Macros for driver mutex locking
 */
#define	TI_GPIO_LOCK(_sc)		mtx_lock_spin(&(_sc)->sc_mtx)
#define	TI_GPIO_UNLOCK(_sc)		mtx_unlock_spin(&(_sc)->sc_mtx)
#define	TI_GPIO_LOCK_INIT(_sc)		\
	mtx_init(&_sc->sc_mtx, device_get_nameunit((_sc)->sc_dev), \
	    "ti_gpio", MTX_SPIN)
#define	TI_GPIO_LOCK_DESTROY(_sc)	mtx_destroy(&(_sc)->sc_mtx)
#define	TI_GPIO_ASSERT_LOCKED(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)
#define	TI_GPIO_ASSERT_UNLOCKED(_sc)	mtx_assert(&(_sc)->sc_mtx, MA_NOTOWNED)

/**
 *	ti_gpio_read_4 - reads a 32-bit value from one of the GPIO registers
 *	@sc: GPIO device context
 *	@bank: The bank to read from
 *	@off: The offset of a register from the GPIO register address range
 *
 *
 *	RETURNS:
 *	32-bit value read from the register.
 */
static inline uint32_t
ti_gpio_read_4(struct ti_gpio_softc *sc, bus_size_t off)
{
	return (bus_read_4(sc->sc_mem_res, off));
}

/**
 *	ti_gpio_write_4 - writes a 32-bit value to one of the GPIO registers
 *	@sc: GPIO device context
 *	@bank: The bank to write to
 *	@off: The offset of a register from the GPIO register address range
 *	@val: The value to write into the register
 *
 *	RETURNS:
 *	nothing
 */
static inline void
ti_gpio_write_4(struct ti_gpio_softc *sc, bus_size_t off,
                 uint32_t val)
{
	bus_write_4(sc->sc_mem_res, off, val);
}

static inline void
ti_gpio_intr_clr(struct ti_gpio_softc *sc, uint32_t mask)
{

	/* We clear both set of registers. */
	ti_gpio_write_4(sc, TI_GPIO_IRQSTATUS_CLR_0, mask);
	ti_gpio_write_4(sc, TI_GPIO_IRQSTATUS_CLR_1, mask);
}

static inline void
ti_gpio_intr_set(struct ti_gpio_softc *sc, uint32_t mask)
{

	/*
	 * On OMAP4 we unmask only the MPU interrupt and on AM335x we
	 * also activate only the first interrupt.
	 */
	ti_gpio_write_4(sc, TI_GPIO_IRQSTATUS_SET_0, mask);
}

static inline void
ti_gpio_intr_ack(struct ti_gpio_softc *sc, uint32_t mask)
{

	/*
	 * Acknowledge the interrupt on both registers even if we use only
	 * the first one.
	 */
	ti_gpio_write_4(sc, TI_GPIO_IRQSTATUS_0, mask);
	ti_gpio_write_4(sc, TI_GPIO_IRQSTATUS_1, mask);
}

static inline uint32_t
ti_gpio_intr_status(struct ti_gpio_softc *sc)
{
	uint32_t reg;

	/* Get the status from both registers. */
	reg = ti_gpio_read_4(sc, TI_GPIO_IRQSTATUS_0);
	reg |= ti_gpio_read_4(sc, TI_GPIO_IRQSTATUS_1);

	return (reg);
}

static device_t
ti_gpio_get_bus(device_t dev)
{
	struct ti_gpio_softc *sc;

	sc = device_get_softc(dev);

	return (sc->sc_busdev);
}

/**
 *	ti_gpio_pin_max - Returns the maximum number of GPIO pins
 *	@dev: gpio device handle
 *	@maxpin: pointer to a value that upon return will contain the maximum number
 *	         of pins in the device.
 *
 *
 *	LOCKING:
 *	No locking required, returns static data.
 *
 *	RETURNS:
 *	Returns 0 on success otherwise an error code
 */
static int
ti_gpio_pin_max(device_t dev, int *maxpin)
{

	*maxpin = PINS_PER_BANK - 1;

	return (0);
}

static int
ti_gpio_valid_pin(struct ti_gpio_softc *sc, int pin)
{

	if (pin >= sc->sc_maxpin || sc->sc_mem_res == NULL)
		return (EINVAL);

	return (0);
}

/**
 *	ti_gpio_pin_getcaps - Gets the capabilities of a given pin
 *	@dev: gpio device handle
 *	@pin: the number of the pin
 *	@caps: pointer to a value that upon return will contain the capabilities
 *
 *	Currently all pins have the same capability, notably:
 *	  - GPIO_PIN_INPUT
 *	  - GPIO_PIN_OUTPUT
 *	  - GPIO_PIN_PULLUP
 *	  - GPIO_PIN_PULLDOWN
 *	  - GPIO_INTR_LEVEL_LOW
 *	  - GPIO_INTR_LEVEL_HIGH
 *	  - GPIO_INTR_EDGE_RISING
 *	  - GPIO_INTR_EDGE_FALLING
 *	  - GPIO_INTR_EDGE_BOTH
 *
 *	LOCKING:
 *	No locking required, returns static data.
 *
 *	RETURNS:
 *	Returns 0 on success otherwise an error code
 */
static int
ti_gpio_pin_getcaps(device_t dev, uint32_t pin, uint32_t *caps)
{
	struct ti_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (ti_gpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	*caps = (GPIO_PIN_INPUT | GPIO_PIN_OUTPUT | GPIO_PIN_PULLUP |
	    GPIO_PIN_PULLDOWN | GPIO_INTR_LEVEL_LOW | GPIO_INTR_LEVEL_HIGH |
	    GPIO_INTR_EDGE_RISING | GPIO_INTR_EDGE_FALLING |
	    GPIO_INTR_EDGE_BOTH);

	return (0);
}

/**
 *	ti_gpio_pin_getflags - Gets the current flags of a given pin
 *	@dev: gpio device handle
 *	@pin: the number of the pin
 *	@flags: upon return will contain the current flags of the pin
 *
 *	Reads the current flags of a given pin, here we actually read the H/W
 *	registers to determine the flags, rather than storing the value in the
 *	setflags call.
 *
 *	LOCKING:
 *	Internally locks the context
 *
 *	RETURNS:
 *	Returns 0 on success otherwise an error code
 */
static int
ti_gpio_pin_getflags(device_t dev, uint32_t pin, uint32_t *flags)
{
	struct ti_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (ti_gpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	/* Get the current pin state */
	TI_GPIO_LOCK(sc);
	TI_GPIO_GET_FLAGS(dev, pin, flags);
	TI_GPIO_UNLOCK(sc);

	return (0);
}

/**
 *	ti_gpio_pin_getname - Gets the name of a given pin
 *	@dev: gpio device handle
 *	@pin: the number of the pin
 *	@name: buffer to put the name in
 *
 *	The driver simply calls the pins gpio_n, where 'n' is obviously the number
 *	of the pin.
 *
 *	LOCKING:
 *	No locking required, returns static data.
 *
 *	RETURNS:
 *	Returns 0 on success otherwise an error code
 */
static int
ti_gpio_pin_getname(device_t dev, uint32_t pin, char *name)
{
	struct ti_gpio_softc *sc;

	sc = device_get_softc(dev);
	if (ti_gpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	/* Set a very simple name */
	snprintf(name, GPIOMAXNAME, "gpio_%u", pin);
	name[GPIOMAXNAME - 1] = '\0';

	return (0);
}

/**
 *	ti_gpio_pin_setflags - Sets the flags for a given pin
 *	@dev: gpio device handle
 *	@pin: the number of the pin
 *	@flags: the flags to set
 *
 *	The flags of the pin correspond to things like input/output mode, pull-ups,
 *	pull-downs, etc.  This driver doesn't support all flags, only the following:
 *	  - GPIO_PIN_INPUT
 *	  - GPIO_PIN_OUTPUT
 *	  - GPIO_PIN_PULLUP
 *	  - GPIO_PIN_PULLDOWN
 *
 *	LOCKING:
 *	Internally locks the context
 *
 *	RETURNS:
 *	Returns 0 on success otherwise an error code
 */
static int
ti_gpio_pin_setflags(device_t dev, uint32_t pin, uint32_t flags)
{
	struct ti_gpio_softc *sc;
	uint32_t oe;

	sc = device_get_softc(dev);
	if (ti_gpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	/* Set the GPIO mode and state */
	TI_GPIO_LOCK(sc);
	if (TI_GPIO_SET_FLAGS(dev, pin, flags) != 0) {
		TI_GPIO_UNLOCK(sc);
		return (EINVAL);
	}

	/* If configuring as an output set the "output enable" bit */
	oe = ti_gpio_read_4(sc, TI_GPIO_OE);
	if (flags & GPIO_PIN_INPUT)
		oe |= TI_GPIO_MASK(pin);
	else
		oe &= ~TI_GPIO_MASK(pin);
	ti_gpio_write_4(sc, TI_GPIO_OE, oe);
	TI_GPIO_UNLOCK(sc);
	
	return (0);
}

/**
 *	ti_gpio_pin_set - Sets the current level on a GPIO pin
 *	@dev: gpio device handle
 *	@pin: the number of the pin
 *	@value: non-zero value will drive the pin high, otherwise the pin is
 *	        driven low.
 *
 *
 *	LOCKING:
 *	Internally locks the context
 *
 *	RETURNS:
 *	Returns 0 on success otherwise a error code
 */
static int
ti_gpio_pin_set(device_t dev, uint32_t pin, unsigned int value)
{
	struct ti_gpio_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);
	if (ti_gpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	TI_GPIO_LOCK(sc);
	if (value == GPIO_PIN_LOW)
		reg = TI_GPIO_CLEARDATAOUT;
	else
		reg = TI_GPIO_SETDATAOUT;
	ti_gpio_write_4(sc, reg, TI_GPIO_MASK(pin));
	TI_GPIO_UNLOCK(sc);

	return (0);
}

/**
 *	ti_gpio_pin_get - Gets the current level on a GPIO pin
 *	@dev: gpio device handle
 *	@pin: the number of the pin
 *	@value: pointer to a value that upond return will contain the pin value
 *
 *	The pin must be configured as an input pin beforehand, otherwise this
 *	function will fail.
 *
 *	LOCKING:
 *	Internally locks the context
 *
 *	RETURNS:
 *	Returns 0 on success otherwise a error code
 */
static int
ti_gpio_pin_get(device_t dev, uint32_t pin, unsigned int *value)
{
	struct ti_gpio_softc *sc;
	uint32_t oe, reg, val;

	sc = device_get_softc(dev);
	if (ti_gpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	/*
	 * Return data from output latch when set as output and from the 
	 * input register otherwise.
	 */
	TI_GPIO_LOCK(sc);
	oe = ti_gpio_read_4(sc, TI_GPIO_OE);
	if (oe & TI_GPIO_MASK(pin))
		reg = TI_GPIO_DATAIN;
	else
		reg = TI_GPIO_DATAOUT;
	val = ti_gpio_read_4(sc, reg);
	*value = (val & TI_GPIO_MASK(pin)) ? 1 : 0;
	TI_GPIO_UNLOCK(sc);

	return (0);
}

/**
 *	ti_gpio_pin_toggle - Toggles a given GPIO pin
 *	@dev: gpio device handle
 *	@pin: the number of the pin
 *
 *
 *	LOCKING:
 *	Internally locks the context
 *
 *	RETURNS:
 *	Returns 0 on success otherwise a error code
 */
static int
ti_gpio_pin_toggle(device_t dev, uint32_t pin)
{
	struct ti_gpio_softc *sc;
	uint32_t reg, val;

	sc = device_get_softc(dev);
	if (ti_gpio_valid_pin(sc, pin) != 0)
		return (EINVAL);

	/* Toggle the pin */
	TI_GPIO_LOCK(sc);
	val = ti_gpio_read_4(sc, TI_GPIO_DATAOUT);
	if (val & TI_GPIO_MASK(pin))
		reg = TI_GPIO_CLEARDATAOUT;
	else
		reg = TI_GPIO_SETDATAOUT;
	ti_gpio_write_4(sc, reg, TI_GPIO_MASK(pin));
	TI_GPIO_UNLOCK(sc);

	return (0);
}

static int
ti_gpio_bank_init(device_t dev)
{
	int pin;
	struct ti_gpio_softc *sc;
	uint32_t flags, reg_oe, reg_set, rev;
	clk_ident_t clk;

	sc = device_get_softc(dev);

	/* Enable the interface and functional clocks for the module. */
	clk = ti_hwmods_get_clock(dev);
	if (clk == INVALID_CLK_IDENT) {
		device_printf(dev, "failed to get device id based on ti,hwmods\n");
		return (EINVAL);
	}

	sc->sc_bank = clk - GPIO1_CLK + ti_first_gpio_bank();
	ti_prcm_clk_enable(clk);

	/*
	 * Read the revision number of the module.  TI don't publish the
	 * actual revision numbers, so instead the values have been
	 * determined by experimentation.
	 */
	rev = ti_gpio_read_4(sc, TI_GPIO_REVISION);

	/* Check the revision. */
	if (rev != ti_gpio_rev()) {
		device_printf(dev, "Warning: could not determine the revision "
		    "of GPIO module (revision:0x%08x)\n", rev);
		return (EINVAL);
	}

	/* Disable interrupts for all pins. */
	ti_gpio_intr_clr(sc, 0xffffffff);

	/* Init OE register based on pads configuration. */
	reg_oe = 0xffffffff;
	reg_set = 0;
	for (pin = 0; pin < PINS_PER_BANK; pin++) {
		TI_GPIO_GET_FLAGS(dev, pin, &flags);
		if (flags & GPIO_PIN_OUTPUT) {
			reg_oe &= ~(1UL << pin);
			if (flags & GPIO_PIN_PULLUP)
				reg_set |= (1UL << pin);
		}
	}
	ti_gpio_write_4(sc, TI_GPIO_OE, reg_oe);
	if (reg_set)
		ti_gpio_write_4(sc, TI_GPIO_SETDATAOUT, reg_set);

	return (0);
}

/**
 *	ti_gpio_attach - attach function for the driver
 *	@dev: gpio device handle
 *
 *	Allocates and sets up the driver context for all GPIO banks.  This function
 *	expects the memory ranges and IRQs to already be allocated to the driver.
 *
 *	LOCKING:
 *	None
 *
 *	RETURNS:
 *	Always returns 0
 */
static int
ti_gpio_attach(device_t dev)
{
	struct ti_gpio_softc *sc;
	int err;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	TI_GPIO_LOCK_INIT(sc);
	ti_gpio_pin_max(dev, &sc->sc_maxpin);
	sc->sc_maxpin++;

	sc->sc_mem_rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_mem_rid, RF_ACTIVE);
	if (!sc->sc_mem_res) {
		device_printf(dev, "Error: could not allocate mem resources\n");
		ti_gpio_detach(dev);
		return (ENXIO);
	}

	sc->sc_irq_rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->sc_irq_rid, RF_ACTIVE);
	if (!sc->sc_irq_res) {
		device_printf(dev, "Error: could not allocate irq resources\n");
		ti_gpio_detach(dev);
		return (ENXIO);
	}

	/*
	 * Register our interrupt filter for each of the IRQ resources.
	 */
	if (bus_setup_intr(dev, sc->sc_irq_res,
	    INTR_TYPE_MISC | INTR_MPSAFE, ti_gpio_intr, NULL, sc,
	    &sc->sc_irq_hdl) != 0) {
		device_printf(dev,
		    "WARNING: unable to register interrupt filter\n");
		ti_gpio_detach(dev);
		return (ENXIO);
	}

	if (ti_gpio_pic_attach(sc) != 0) {
		device_printf(dev, "WARNING: unable to attach PIC\n");
		ti_gpio_detach(dev);
		return (ENXIO);
	}

	/* We need to go through each block and ensure the clocks are running and
	 * the module is enabled.  It might be better to do this only when the
	 * pins are configured which would result in less power used if the GPIO
	 * pins weren't used ... 
	 */
	if (sc->sc_mem_res != NULL) {
		/* Initialize the GPIO module. */
		err = ti_gpio_bank_init(dev);
		if (err != 0) {
			ti_gpio_detach(dev);
			return (err);
		}
	}

	sc->sc_busdev = gpiobus_attach_bus(dev);
	if (sc->sc_busdev == NULL) {
		ti_gpio_detach(dev);
		return (ENXIO);
	}

	return (0);
}

/**
 *	ti_gpio_detach - detach function for the driver
 *	@dev: scm device handle
 *
 *	Allocates and sets up the driver context, this simply entails creating a
 *	bus mappings for the SCM register set.
 *
 *	LOCKING:
 *	None
 *
 *	RETURNS:
 *	Always returns 0
 */
static int
ti_gpio_detach(device_t dev)
{
	struct ti_gpio_softc *sc = device_get_softc(dev);

	KASSERT(mtx_initialized(&sc->sc_mtx), ("gpio mutex not initialized"));

	/* Disable all interrupts */
	if (sc->sc_mem_res != NULL)
		ti_gpio_intr_clr(sc, 0xffffffff);
	if (sc->sc_busdev != NULL)
		gpiobus_detach_bus(dev);
	if (sc->sc_isrcs != NULL)
		ti_gpio_pic_detach(sc);
	/* Release the memory and IRQ resources. */
	if (sc->sc_irq_hdl) {
		bus_teardown_intr(dev, sc->sc_irq_res,
		    sc->sc_irq_hdl);
	}
	if (sc->sc_irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irq_rid,
		    sc->sc_irq_res);
	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_mem_rid,
		    sc->sc_mem_res);
	TI_GPIO_LOCK_DESTROY(sc);

	return (0);
}

static inline void
ti_gpio_rwreg_modify(struct ti_gpio_softc *sc, uint32_t reg, uint32_t mask,
    bool set_bits)
{
	uint32_t value;

	value = ti_gpio_read_4(sc, reg);
	ti_gpio_write_4(sc, reg, set_bits ? value | mask : value & ~mask);
}

static inline void
ti_gpio_isrc_mask(struct ti_gpio_softc *sc, struct ti_gpio_irqsrc *tgi)
{

	/* Writing a 0 has no effect. */
	ti_gpio_intr_clr(sc, tgi->tgi_mask);
}

static inline void
ti_gpio_isrc_unmask(struct ti_gpio_softc *sc, struct ti_gpio_irqsrc *tgi)
{

	/* Writing a 0 has no effect. */
	ti_gpio_intr_set(sc, tgi->tgi_mask);
}

static inline void
ti_gpio_isrc_eoi(struct ti_gpio_softc *sc, struct ti_gpio_irqsrc *tgi)
{

	/* Writing a 0 has no effect. */
	ti_gpio_intr_ack(sc, tgi->tgi_mask);
}

static inline bool
ti_gpio_isrc_is_level(struct ti_gpio_irqsrc *tgi)
{

	return (tgi->tgi_mode == GPIO_INTR_LEVEL_LOW ||
	    tgi->tgi_mode == GPIO_INTR_LEVEL_HIGH);
}

static int
ti_gpio_intr(void *arg)
{
	u_int irq;
	uint32_t reg;
	struct ti_gpio_softc *sc;
	struct trapframe *tf;
	struct ti_gpio_irqsrc *tgi;

	sc = (struct ti_gpio_softc *)arg;
	tf = curthread->td_intr_frame;

	reg = ti_gpio_intr_status(sc);
	for (irq = 0; irq < sc->sc_maxpin; irq++) {
		tgi = &sc->sc_isrcs[irq];
		if ((reg & tgi->tgi_mask) == 0)
			continue;
		if (!ti_gpio_isrc_is_level(tgi))
			ti_gpio_isrc_eoi(sc, tgi);
		if (intr_isrc_dispatch(&tgi->tgi_isrc, tf) != 0) {
			ti_gpio_isrc_mask(sc, tgi);
			if (ti_gpio_isrc_is_level(tgi))
				ti_gpio_isrc_eoi(sc, tgi);
			device_printf(sc->sc_dev, "Stray irq %u disabled\n",
			    irq);
		}
	}
	return (FILTER_HANDLED);
}

static int
ti_gpio_pic_attach(struct ti_gpio_softc *sc)
{
	int error;
	uint32_t irq;
	const char *name;

	sc->sc_isrcs = malloc(sizeof(*sc->sc_isrcs) * sc->sc_maxpin, M_DEVBUF,
	    M_WAITOK | M_ZERO);

	name = device_get_nameunit(sc->sc_dev);
	for (irq = 0; irq < sc->sc_maxpin; irq++) {
		sc->sc_isrcs[irq].tgi_irq = irq;
		sc->sc_isrcs[irq].tgi_mask = TI_GPIO_MASK(irq);
		sc->sc_isrcs[irq].tgi_mode = GPIO_INTR_CONFORM;

		error = intr_isrc_register(&sc->sc_isrcs[irq].tgi_isrc,
		    sc->sc_dev, 0, "%s,%u", name, irq);
		if (error != 0)
			return (error); /* XXX deregister ISRCs */
	}
	if (intr_pic_register(sc->sc_dev,
	    OF_xref_from_node(ofw_bus_get_node(sc->sc_dev))) == NULL)
		return (ENXIO);

	return (0);
}

static int
ti_gpio_pic_detach(struct ti_gpio_softc *sc)
{

	/*
	 *  There has not been established any procedure yet
	 *  how to detach PIC from living system correctly.
	 */
	device_printf(sc->sc_dev, "%s: not implemented yet\n", __func__);
	return (EBUSY);
}

static void
ti_gpio_pic_config_intr(struct ti_gpio_softc *sc, struct ti_gpio_irqsrc *tgi,
    uint32_t mode)
{

	TI_GPIO_LOCK(sc);
	ti_gpio_rwreg_modify(sc, TI_GPIO_RISINGDETECT, tgi->tgi_mask,
	    mode == GPIO_INTR_EDGE_RISING || mode == GPIO_INTR_EDGE_BOTH);
	ti_gpio_rwreg_modify(sc, TI_GPIO_FALLINGDETECT, tgi->tgi_mask,
	    mode == GPIO_INTR_EDGE_FALLING || mode == GPIO_INTR_EDGE_BOTH);
	ti_gpio_rwreg_modify(sc, TI_GPIO_LEVELDETECT1, tgi->tgi_mask,
	    mode == GPIO_INTR_LEVEL_HIGH);
	ti_gpio_rwreg_modify(sc, TI_GPIO_LEVELDETECT0, tgi->tgi_mask,
	    mode == GPIO_INTR_LEVEL_LOW);
	tgi->tgi_mode = mode;
	TI_GPIO_UNLOCK(sc);
}

static void
ti_gpio_pic_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct ti_gpio_softc *sc = device_get_softc(dev);
	struct ti_gpio_irqsrc *tgi = (struct ti_gpio_irqsrc *)isrc;

	ti_gpio_isrc_mask(sc, tgi);
}

static void
ti_gpio_pic_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct ti_gpio_softc *sc = device_get_softc(dev);
	struct ti_gpio_irqsrc *tgi = (struct ti_gpio_irqsrc *)isrc;

	arm_irq_memory_barrier(tgi->tgi_irq);
	ti_gpio_isrc_unmask(sc, tgi);
}

static int
ti_gpio_pic_map_fdt(struct ti_gpio_softc *sc, struct intr_map_data_fdt *daf,
    u_int *irqp, uint32_t *modep)
{
	uint32_t mode;

	/*
	 * The first cell is the interrupt number.
	 * The second cell is used to specify flags:
	 *	bits[3:0] trigger type and level flags:
	 *		1 = low-to-high edge triggered.
	 *		2 = high-to-low edge triggered.
	 *		4 = active high level-sensitive.
	 *		8 = active low level-sensitive.
	 */
	if (daf->ncells != 2 || daf->cells[0] >= sc->sc_maxpin)
		return (EINVAL);

	/* Only reasonable modes are supported. */
	if (daf->cells[1] == 1)
		mode = GPIO_INTR_EDGE_RISING;
	else if (daf->cells[1] == 2)
		mode = GPIO_INTR_EDGE_FALLING;
	else if (daf->cells[1] == 3)
		mode = GPIO_INTR_EDGE_BOTH;
	else if (daf->cells[1] == 4)
		mode = GPIO_INTR_LEVEL_HIGH;
	else if (daf->cells[1] == 8)
		mode = GPIO_INTR_LEVEL_LOW;
	else
		return (EINVAL);

	*irqp = daf->cells[0];
	if (modep != NULL)
		*modep = mode;
	return (0);
}

static int
ti_gpio_pic_map_gpio(struct ti_gpio_softc *sc, struct intr_map_data_gpio *dag,
    u_int *irqp, uint32_t *modep)
{
	uint32_t mode;

	if (dag->gpio_pin_num >= sc->sc_maxpin)
		return (EINVAL);

	mode = dag->gpio_intr_mode;
	if (mode != GPIO_INTR_LEVEL_LOW && mode != GPIO_INTR_LEVEL_HIGH &&
	    mode != GPIO_INTR_EDGE_RISING && mode != GPIO_INTR_EDGE_FALLING &&
	    mode != GPIO_INTR_EDGE_BOTH)
		return (EINVAL);

	*irqp = dag->gpio_pin_num;
	if (modep != NULL)
		*modep = mode;
	return (0);
}

static int
ti_gpio_pic_map(struct ti_gpio_softc *sc, struct intr_map_data *data,
    u_int *irqp, uint32_t *modep)
{

	switch (data->type) {
	case INTR_MAP_DATA_FDT:
		return (ti_gpio_pic_map_fdt(sc,
		    (struct intr_map_data_fdt *)data, irqp, modep));
	case INTR_MAP_DATA_GPIO:
		return (ti_gpio_pic_map_gpio(sc,
		    (struct intr_map_data_gpio *)data, irqp, modep));
	default:
		return (ENOTSUP);
	}
}

static int
ti_gpio_pic_map_intr(device_t dev, struct intr_map_data *data,
    struct intr_irqsrc **isrcp)
{
	int error;
	u_int irq;
	struct ti_gpio_softc *sc = device_get_softc(dev);

	error = ti_gpio_pic_map(sc, data, &irq, NULL);
	if (error == 0)
		*isrcp = &sc->sc_isrcs[irq].tgi_isrc;
	return (error);
}

static void
ti_gpio_pic_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
	struct ti_gpio_softc *sc = device_get_softc(dev);
	struct ti_gpio_irqsrc *tgi = (struct ti_gpio_irqsrc *)isrc;

	if (ti_gpio_isrc_is_level(tgi))
		ti_gpio_isrc_eoi(sc, tgi);
}

static void
ti_gpio_pic_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{

	ti_gpio_pic_enable_intr(dev, isrc);
}

static void
ti_gpio_pic_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
	struct ti_gpio_softc *sc = device_get_softc(dev);
	struct ti_gpio_irqsrc *tgi = (struct ti_gpio_irqsrc *)isrc;

	ti_gpio_isrc_mask(sc, tgi);
	if (ti_gpio_isrc_is_level(tgi))
		ti_gpio_isrc_eoi(sc, tgi);
}

static int
ti_gpio_pic_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	u_int irq;
	uint32_t mode;
	struct ti_gpio_softc *sc;
	struct ti_gpio_irqsrc *tgi;

	if (data == NULL)
		return (ENOTSUP);

	sc = device_get_softc(dev);
	tgi = (struct ti_gpio_irqsrc *)isrc;

	/* Get and check config for an interrupt. */
	if (ti_gpio_pic_map(sc, data, &irq, &mode) != 0 || tgi->tgi_irq != irq)
		return (EINVAL);

	/*
	 * If this is a setup for another handler,
	 * only check that its configuration match.
	 */
	if (isrc->isrc_handlers != 0)
		return (tgi->tgi_mode == mode ? 0 : EINVAL);

	ti_gpio_pic_config_intr(sc, tgi, mode);
	return (0);
}

static int
ti_gpio_pic_teardown_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct ti_gpio_softc *sc = device_get_softc(dev);
	struct ti_gpio_irqsrc *tgi = (struct ti_gpio_irqsrc *)isrc;

	if (isrc->isrc_handlers == 0)
		ti_gpio_pic_config_intr(sc, tgi, GPIO_INTR_CONFORM);
	return (0);
}

static phandle_t
ti_gpio_get_node(device_t bus, device_t dev)
{

	/* We only have one child, the GPIO bus, which needs our own node. */
	return (ofw_bus_get_node(bus));
}

static device_method_t ti_gpio_methods[] = {
	DEVMETHOD(device_attach, ti_gpio_attach),
	DEVMETHOD(device_detach, ti_gpio_detach),

	/* GPIO protocol */
	DEVMETHOD(gpio_get_bus, ti_gpio_get_bus),
	DEVMETHOD(gpio_pin_max, ti_gpio_pin_max),
	DEVMETHOD(gpio_pin_getname, ti_gpio_pin_getname),
	DEVMETHOD(gpio_pin_getflags, ti_gpio_pin_getflags),
	DEVMETHOD(gpio_pin_getcaps, ti_gpio_pin_getcaps),
	DEVMETHOD(gpio_pin_setflags, ti_gpio_pin_setflags),
	DEVMETHOD(gpio_pin_get, ti_gpio_pin_get),
	DEVMETHOD(gpio_pin_set, ti_gpio_pin_set),
	DEVMETHOD(gpio_pin_toggle, ti_gpio_pin_toggle),

	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,	ti_gpio_pic_disable_intr),
	DEVMETHOD(pic_enable_intr,	ti_gpio_pic_enable_intr),
	DEVMETHOD(pic_map_intr,		ti_gpio_pic_map_intr),
	DEVMETHOD(pic_setup_intr,	ti_gpio_pic_setup_intr),
	DEVMETHOD(pic_teardown_intr,	ti_gpio_pic_teardown_intr),
	DEVMETHOD(pic_post_filter,	ti_gpio_pic_post_filter),
	DEVMETHOD(pic_post_ithread,	ti_gpio_pic_post_ithread),
	DEVMETHOD(pic_pre_ithread,	ti_gpio_pic_pre_ithread),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node, ti_gpio_get_node),

	{0, 0},
};

driver_t ti_gpio_driver = {
	"gpio",
	ti_gpio_methods,
	sizeof(struct ti_gpio_softc),
};
