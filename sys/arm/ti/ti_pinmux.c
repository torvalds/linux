/*
 * Copyright (c) 2010
 *	Ben Gray <ben.r.gray@gmail.com>.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Ben Gray.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BEN GRAY ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BEN GRAY BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * Exposes pinmux module to pinctrl-compatible interface
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/fdt/fdt_pinctrl.h>

#include <arm/ti/omap4/omap4_scm_padconf.h>
#include <arm/ti/am335x/am335x_scm_padconf.h>
#include <arm/ti/ti_cpuid.h>
#include "ti_pinmux.h"

struct pincfg {
	uint32_t reg;
	uint32_t conf;
};

static struct resource_spec ti_pinmux_res_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },	/* Control memory window */
	{ -1, 0 }
};

static struct ti_pinmux_softc *ti_pinmux_sc;

#define	ti_pinmux_read_2(sc, reg)		\
    bus_space_read_2((sc)->sc_bst, (sc)->sc_bsh, (reg))
#define	ti_pinmux_write_2(sc, reg, val)		\
    bus_space_write_2((sc)->sc_bst, (sc)->sc_bsh, (reg), (val))
#define	ti_pinmux_read_4(sc, reg)		\
    bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (reg))
#define	ti_pinmux_write_4(sc, reg, val)		\
    bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (reg), (val))


/**
 *	ti_padconf_devmap - Array of pins, should be defined one per SoC
 *
 *	This array is typically defined in one of the targeted *_scm_pinumx.c
 *	files and is specific to the given SoC platform. Each entry in the array
 *	corresponds to an individual pin.
 */
static const struct ti_pinmux_device *ti_pinmux_dev;


/**
 *	ti_pinmux_padconf_from_name - searches the list of pads and returns entry
 *	                             with matching ball name.
 *	@ballname: the name of the ball
 *
 *	RETURNS:
 *	A pointer to the matching padconf or NULL if the ball wasn't found.
 */
static const struct ti_pinmux_padconf*
ti_pinmux_padconf_from_name(const char *ballname)
{
	const struct ti_pinmux_padconf *padconf;

	padconf = ti_pinmux_dev->padconf;
	while (padconf->ballname != NULL) {
		if (strcmp(ballname, padconf->ballname) == 0)
			return(padconf);
		padconf++;
	}

	return (NULL);
}

/**
 *	ti_pinmux_padconf_set_internal - sets the muxmode and state for a pad/pin
 *	@padconf: pointer to the pad structure
 *	@muxmode: the name of the mode to use for the pin, i.e. "uart1_rx"
 *	@state: the state to put the pad/pin in, i.e. PADCONF_PIN_???
 *
 *
 *	LOCKING:
 *	Internally locks it's own context.
 *
 *	RETURNS:
 *	0 on success.
 *	EINVAL if pin requested is outside valid range or already in use.
 */
static int
ti_pinmux_padconf_set_internal(struct ti_pinmux_softc *sc,
    const struct ti_pinmux_padconf *padconf,
    const char *muxmode, unsigned int state)
{
	unsigned int mode;
	uint16_t reg_val;

	/* populate the new value for the PADCONF register */
	reg_val = (uint16_t)(state & ti_pinmux_dev->padconf_sate_mask);

	/* find the new mode requested */
	for (mode = 0; mode < 8; mode++) {
		if ((padconf->muxmodes[mode] != NULL) &&
		    (strcmp(padconf->muxmodes[mode], muxmode) == 0)) {
			break;
		}
	}

	/* couldn't find the mux mode */
	if (mode >= 8) {
		printf("Invalid mode \"%s\"\n", muxmode);
		return (EINVAL);
	}

	/* set the mux mode */
	reg_val |= (uint16_t)(mode & ti_pinmux_dev->padconf_muxmode_mask);

	if (bootverbose)
		device_printf(sc->sc_dev, "setting internal %x for %s\n",
		    reg_val, muxmode);
	/* write the register value (16-bit writes) */
	ti_pinmux_write_2(sc, padconf->reg_off, reg_val);

	return (0);
}

/**
 *	ti_pinmux_padconf_set - sets the muxmode and state for a pad/pin
 *	@padname: the name of the pad, i.e. "c12"
 *	@muxmode: the name of the mode to use for the pin, i.e. "uart1_rx"
 *	@state: the state to put the pad/pin in, i.e. PADCONF_PIN_???
 *
 *
 *	LOCKING:
 *	Internally locks it's own context.
 *
 *	RETURNS:
 *	0 on success.
 *	EINVAL if pin requested is outside valid range or already in use.
 */
int
ti_pinmux_padconf_set(const char *padname, const char *muxmode, unsigned int state)
{
	const struct ti_pinmux_padconf *padconf;

	if (!ti_pinmux_sc)
		return (ENXIO);

	/* find the pin in the devmap */
	padconf = ti_pinmux_padconf_from_name(padname);
	if (padconf == NULL)
		return (EINVAL);

	return (ti_pinmux_padconf_set_internal(ti_pinmux_sc, padconf, muxmode, state));
}

/**
 *	ti_pinmux_padconf_get - gets the muxmode and state for a pad/pin
 *	@padname: the name of the pad, i.e. "c12"
 *	@muxmode: upon return will contain the name of the muxmode of the pin
 *	@state: upon return will contain the state of the pad/pin
 *
 *
 *	LOCKING:
 *	Internally locks it's own context.
 *
 *	RETURNS:
 *	0 on success.
 *	EINVAL if pin requested is outside valid range or already in use.
 */
int
ti_pinmux_padconf_get(const char *padname, const char **muxmode,
    unsigned int *state)
{
	const struct ti_pinmux_padconf *padconf;
	uint16_t reg_val;

	if (!ti_pinmux_sc)
		return (ENXIO);

	/* find the pin in the devmap */
	padconf = ti_pinmux_padconf_from_name(padname);
	if (padconf == NULL)
		return (EINVAL);

	/* read the register value (16-bit reads) */
	reg_val = ti_pinmux_read_2(ti_pinmux_sc, padconf->reg_off);

	/* save the state */
	if (state)
		*state = (reg_val & ti_pinmux_dev->padconf_sate_mask);

	/* save the mode */
	if (muxmode)
		*muxmode = padconf->muxmodes[(reg_val & ti_pinmux_dev->padconf_muxmode_mask)];

	return (0);
}

/**
 *	ti_pinmux_padconf_set_gpiomode - converts a pad to GPIO mode.
 *	@gpio: the GPIO pin number (0-195)
 *	@state: the state to put the pad/pin in, i.e. PADCONF_PIN_???
 *
 *
 *
 *	LOCKING:
 *	Internally locks it's own context.
 *
 *	RETURNS:
 *	0 on success.
 *	EINVAL if pin requested is outside valid range or already in use.
 */
int
ti_pinmux_padconf_set_gpiomode(uint32_t gpio, unsigned int state)
{
	const struct ti_pinmux_padconf *padconf;
	uint16_t reg_val;

	if (!ti_pinmux_sc)
		return (ENXIO);

	/* find the gpio pin in the padconf array */
	padconf = ti_pinmux_dev->padconf;
	while (padconf->ballname != NULL) {
		if (padconf->gpio_pin == gpio)
			break;
		padconf++;
	}
	if (padconf->ballname == NULL)
		return (EINVAL);

	/* populate the new value for the PADCONF register */
	reg_val = (uint16_t)(state & ti_pinmux_dev->padconf_sate_mask);

	/* set the mux mode */
	reg_val |= (uint16_t)(padconf->gpio_mode & ti_pinmux_dev->padconf_muxmode_mask);

	/* write the register value (16-bit writes) */
	ti_pinmux_write_2(ti_pinmux_sc, padconf->reg_off, reg_val);

	return (0);
}

/**
 *	ti_pinmux_padconf_get_gpiomode - gets the current GPIO mode of the pin
 *	@gpio: the GPIO pin number (0-195)
 *	@state: upon return will contain the state
 *
 *
 *
 *	LOCKING:
 *	Internally locks it's own context.
 *
 *	RETURNS:
 *	0 on success.
 *	EINVAL if pin requested is outside valid range or not configured as GPIO.
 */
int
ti_pinmux_padconf_get_gpiomode(uint32_t gpio, unsigned int *state)
{
	const struct ti_pinmux_padconf *padconf;
	uint16_t reg_val;

	if (!ti_pinmux_sc)
		return (ENXIO);

	/* find the gpio pin in the padconf array */
	padconf = ti_pinmux_dev->padconf;
	while (padconf->ballname != NULL) {
		if (padconf->gpio_pin == gpio)
			break;
		padconf++;
	}
	if (padconf->ballname == NULL)
		return (EINVAL);

	/* read the current register settings */
	reg_val = ti_pinmux_read_2(ti_pinmux_sc, padconf->reg_off);

	/* check to make sure the pins is configured as GPIO in the first state */
	if ((reg_val & ti_pinmux_dev->padconf_muxmode_mask) != padconf->gpio_mode)
		return (EINVAL);

	/* read and store the reset of the state, i.e. pull-up, pull-down, etc */
	if (state)
		*state = (reg_val & ti_pinmux_dev->padconf_sate_mask);

	return (0);
}

static int
ti_pinmux_configure_pins(device_t dev, phandle_t cfgxref)
{
	struct pincfg *cfgtuples, *cfg;
	phandle_t cfgnode;
	int i, ntuples;
	static struct ti_pinmux_softc *sc;

	sc = device_get_softc(dev);
	cfgnode = OF_node_from_xref(cfgxref);
	ntuples = OF_getencprop_alloc_multi(cfgnode, "pinctrl-single,pins",
	    sizeof(*cfgtuples), (void **)&cfgtuples);

	if (ntuples < 0)
		return (ENOENT);

	if (ntuples == 0)
		return (0); /* Empty property is not an error. */

	for (i = 0, cfg = cfgtuples; i < ntuples; i++, cfg++) {
		if (bootverbose) {
			char name[32];
			OF_getprop(cfgnode, "name", &name, sizeof(name));
			printf("%16s: muxreg 0x%04x muxval 0x%02x\n",
			    name, cfg->reg, cfg->conf);
		}

		/* write the register value (16-bit writes) */
		ti_pinmux_write_2(sc, cfg->reg, cfg->conf);
	}

	OF_prop_free(cfgtuples);

	return (0);
}

/*
 * Device part of OMAP SCM driver
 */

static int
ti_pinmux_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "pinctrl-single"))
		return (ENXIO);

	if (ti_pinmux_sc) {
		printf("%s: multiple pinctrl modules in device tree data, ignoring\n",
		    __func__);
		return (EEXIST);
	}
	switch (ti_chip()) {
#ifdef SOC_OMAP4
	case CHIP_OMAP_4:
		ti_pinmux_dev = &omap4_pinmux_dev;
		break;
#endif
#ifdef SOC_TI_AM335X
	case CHIP_AM335X:
		ti_pinmux_dev = &ti_am335x_pinmux_dev;
		break;
#endif
	default:
		printf("Unknown CPU in pinmux\n");
		return (ENXIO);
	}


	device_set_desc(dev, "TI Pinmux Module");
	return (BUS_PROBE_DEFAULT);
}

/**
 *	ti_pinmux_attach - attaches the pinmux to the simplebus
 *	@dev: new device
 *
 *	RETURNS
 *	Zero on success or ENXIO if an error occuried.
 */
static int
ti_pinmux_attach(device_t dev)
{
	struct ti_pinmux_softc *sc = device_get_softc(dev);

#if 0
	if (ti_pinmux_sc)
		return (ENXIO);
#endif

	sc->sc_dev = dev;

	if (bus_alloc_resources(dev, ti_pinmux_res_spec, sc->sc_res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->sc_bst = rman_get_bustag(sc->sc_res[0]);
	sc->sc_bsh = rman_get_bushandle(sc->sc_res[0]);

	if (ti_pinmux_sc == NULL)
		ti_pinmux_sc = sc;

	fdt_pinctrl_register(dev, "pinctrl-single,pins");
	fdt_pinctrl_configure_tree(dev);

	return (0);
}

static device_method_t ti_pinmux_methods[] = {
	DEVMETHOD(device_probe,		ti_pinmux_probe),
	DEVMETHOD(device_attach,	ti_pinmux_attach),

        /* fdt_pinctrl interface */
	DEVMETHOD(fdt_pinctrl_configure, ti_pinmux_configure_pins),
	{ 0, 0 }
};

static driver_t ti_pinmux_driver = {
	"ti_pinmux",
	ti_pinmux_methods,
	sizeof(struct ti_pinmux_softc),
};

static devclass_t ti_pinmux_devclass;

DRIVER_MODULE(ti_pinmux, simplebus, ti_pinmux_driver, ti_pinmux_devclass, 0, 0);
