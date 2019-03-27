/*-
 * Copyright 2015 John Wehle <john@feith.com>
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
 */

/*
 * Amlogic aml8726 pinctrl driver.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/fdt/fdt_pinctrl.h>

#include <arm/amlogic/aml8726/aml8726_soc.h>
#include <arm/amlogic/aml8726/aml8726_pinctrl.h>

struct aml8726_pinctrl_softc {
	device_t				dev;
	struct {
		struct aml8726_pinctrl_function	*func;
		struct aml8726_pinctrl_pkg_pin	*ppin;
		boolean_t			pud_ctrl;
	}					soc;
	struct resource				*res[6];
	struct mtx				mtx;
};

static struct resource_spec aml8726_pinctrl_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE }, /* mux */
	{ SYS_RES_MEMORY, 1, RF_ACTIVE | RF_SHAREABLE }, /* pu/pd */
	{ SYS_RES_MEMORY, 2, RF_ACTIVE | RF_SHAREABLE }, /* pull enable */
	{ SYS_RES_MEMORY, 3, RF_ACTIVE }, /* ao mux */
	{ SYS_RES_MEMORY, 4, RF_ACTIVE | RF_SHAREABLE }, /* ao pu/pd */
	{ SYS_RES_MEMORY, 5, RF_ACTIVE | RF_SHAREABLE }, /* ao pull enable */
	{ -1, 0 }
};

#define	AML_PINCTRL_LOCK(sc)		mtx_lock(&(sc)->mtx)
#define	AML_PINCTRL_UNLOCK(sc)		mtx_unlock(&(sc)->mtx)
#define	AML_PINCTRL_LOCK_INIT(sc)	\
    mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev),	\
    "pinctrl", MTX_DEF)
#define	AML_PINCTRL_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx);

#define	MUX_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	MUX_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)

#define	PUD_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[1], reg, (val))
#define	PUD_READ_4(sc, reg)		bus_read_4((sc)->res[1], reg)

#define	PEN_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[2], reg, (val))
#define	PEN_READ_4(sc, reg)		bus_read_4((sc)->res[2], reg)

#define	AOMUX_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[3], reg, (val))
#define	AOMUX_READ_4(sc, reg)		bus_read_4((sc)->res[3], reg)

#define	AOPUD_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[4], reg, (val))
#define	AOPUD_READ_4(sc, reg)		bus_read_4((sc)->res[4], reg)

#define	AOPEN_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[5], reg, (val))
#define	AOPEN_READ_4(sc, reg)		bus_read_4((sc)->res[5], reg)

static int
aml8726_pinctrl_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,aml8726-pinctrl"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726 pinctrl");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_pinctrl_attach(device_t dev)
{
	struct aml8726_pinctrl_softc *sc = device_get_softc(dev);

	sc->dev = dev;

	sc->soc.pud_ctrl = false;

	switch (aml8726_soc_hw_rev) {
	case AML_SOC_HW_REV_M3:
		sc->soc.func = aml8726_m3_pinctrl;
		sc->soc.ppin = aml8726_m3_pkg_pin;
		break;
	case AML_SOC_HW_REV_M6:
		sc->soc.func = aml8726_m6_pinctrl;
		sc->soc.ppin = aml8726_m6_pkg_pin;
		break;
	case AML_SOC_HW_REV_M8:
		sc->soc.func = aml8726_m8_pinctrl;
		sc->soc.ppin = aml8726_m8_pkg_pin;
		sc->soc.pud_ctrl = true;
		break;
	case AML_SOC_HW_REV_M8B:
		sc->soc.func = aml8726_m8b_pinctrl;
		sc->soc.ppin = aml8726_m8b_pkg_pin;
		sc->soc.pud_ctrl = true;
		break;
	default:
		device_printf(dev, "unsupported SoC\n");
		return (ENXIO);
		/* NOTREACHED */
	}

	if (bus_alloc_resources(dev, aml8726_pinctrl_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		return (ENXIO);
	}

	AML_PINCTRL_LOCK_INIT(sc);

	fdt_pinctrl_register(dev, "amlogic,pins");
	fdt_pinctrl_configure_tree(dev);

	return (0);
}

static int
aml8726_pinctrl_detach(device_t dev)
{
	struct aml8726_pinctrl_softc *sc = device_get_softc(dev);

	AML_PINCTRL_LOCK_DESTROY(sc);

	bus_release_resources(dev, aml8726_pinctrl_spec, sc->res);

	return (0);
}


static int
aml8726_pinctrl_configure_pins(device_t dev, phandle_t cfgxref)
{
	struct aml8726_pinctrl_softc *sc = device_get_softc(dev);
	struct aml8726_pinctrl_function *cf;
	struct aml8726_pinctrl_function *f;
	struct aml8726_pinctrl_pkg_pin *pp;
	struct aml8726_pinctrl_pin *cp;
	struct aml8726_pinctrl_pin *p;
	enum aml8726_pinctrl_pull_mode pm;
	char *function_name;
	char *pins;
	char *pin_name;
	char *pull;
	phandle_t node;
	ssize_t len;
	uint32_t value;

	node = OF_node_from_xref(cfgxref);

	len = OF_getprop_alloc(node, "amlogic,function",
	    (void **)&function_name);

	if (len < 0) {
		device_printf(dev,
		    "missing amlogic,function attribute in FDT\n");
		return (ENXIO);
	}

	for (f = sc->soc.func; f->name != NULL; f++)
		if (strncmp(f->name, function_name, len) == 0)
			break;

	if (f->name == NULL) {
		device_printf(dev, "unknown function attribute %.*s in FDT\n",
		    len, function_name);
		OF_prop_free(function_name);
		return (ENXIO);
	}

	OF_prop_free(function_name);

	len = OF_getprop_alloc(node, "amlogic,pull",
	    (void **)&pull);

	pm = aml8726_unknown_pm;

	if (len > 0) {
		if (strncmp(pull, "enable", len) == 0)
			pm = aml8726_enable_pm;
		else if (strncmp(pull, "disable", len) == 0)
			pm = aml8726_disable_pm;
		else if (strncmp(pull, "down", len) == 0)
			pm = aml8726_enable_down_pm;
		else if (strncmp(pull, "up", len) == 0)
			pm = aml8726_enable_up_pm;
		else {
			device_printf(dev,
			    "unknown pull attribute %.*s in FDT\n",
			    len, pull);
			OF_prop_free(pull);
			return (ENXIO);
		}
	}

	OF_prop_free(pull);

	/*
	 * Setting the pull direction isn't supported on all SoC.
	 */
	switch (pm) {
	case aml8726_enable_down_pm:
	case aml8726_enable_up_pm:
		if (sc->soc.pud_ctrl == false) {
			device_printf(dev,
			    "SoC doesn't support setting pull direction.\n");
			return (ENXIO);
		}
		break;
	default:
		break;
	}

	len = OF_getprop_alloc(node, "amlogic,pins",
	    (void **)&pins);

	if (len < 0) {
		device_printf(dev, "missing amlogic,pins attribute in FDT\n");
		return (ENXIO);
	}

	pin_name = pins;

	while (len) {
		for (p = f->pins; p->name != NULL; p++)
			if (strncmp(p->name, pin_name, len) == 0)
				break;

		if (p->name == NULL) {
			/* display message prior to queuing up next string */
			device_printf(dev, "unknown pin attribute %.*s in FDT\n",
			    len, pin_name);
		}

		/* queue up next string */
		while (*pin_name && len) {
			pin_name++;
			len--;
		}
		if (len) {
			pin_name++;
			len--;
		}

		if (p->name == NULL)
			continue;

		for (pp = sc->soc.ppin; pp->pkg_name != NULL; pp++)
			if (strcmp(pp->pkg_name, p->pkg_name) == 0)
				break;

		if (pp->pkg_name == NULL) {
			device_printf(dev,
			    "missing entry for package pin %s\n",
			    p->pkg_name);
			continue;
		}

		if (pm != aml8726_unknown_pm && pp->pull_bits == 0x00000000) {
			device_printf(dev,
			    "missing pull info for package pin %s\n",
			    p->pkg_name);
			continue;
		}

		AML_PINCTRL_LOCK(sc);

		/*
		 * First clear all other mux bits associated with this
		 * package pin.  This may briefly configure the pin as
		 * GPIO ...  however this should be fine since after
		 * reset the default GPIO mode is input.
		 */

		for (cf = sc->soc.func; cf->name != NULL; cf++)
			for (cp = cf->pins; cp->name != NULL; cp++) {
				if (cp == p)
					continue;
				if (strcmp(cp->pkg_name, p->pkg_name) != 0)
					continue;
				if (cp->mux_bits == 0)
					continue;
				if (pp->aobus == false) {
					value = MUX_READ_4(sc, cp->mux_addr);
					value &= ~cp->mux_bits;
					MUX_WRITE_4(sc, cp->mux_addr, value);
				} else {
					value = AOMUX_READ_4(sc, cp->mux_addr);
					value &= ~cp->mux_bits;
					AOMUX_WRITE_4(sc, cp->mux_addr, value);
				}
			}

		/*
		 * Now set the desired mux bits.
		 *
		 * In the case of GPIO there's no bits to set.
		 */

		if (p->mux_bits != 0) {
			if (pp->aobus == false) {
				value = MUX_READ_4(sc, p->mux_addr);
				value |= p->mux_bits;
				MUX_WRITE_4(sc, p->mux_addr, value);
			} else {
				value = AOMUX_READ_4(sc, p->mux_addr);
				value |= p->mux_bits;
				AOMUX_WRITE_4(sc, p->mux_addr, value);
			}
		}

		/*
		 * Finally set the pull mode if it was specified.
		 */

		switch (pm) {
		case aml8726_enable_down_pm:
		case aml8726_enable_up_pm:
			if (pp->aobus == false) {
				value = PUD_READ_4(sc, pp->pull_addr);
				if (pm == aml8726_enable_down_pm)
					value &= ~pp->pull_bits;
				else
					value |= pp->pull_bits;
				PUD_WRITE_4(sc, pp->pull_addr, value);
			} else {
				value = AOPUD_READ_4(sc, pp->pull_addr);
				if (pm == aml8726_enable_down_pm)
					value &= ~(pp->pull_bits << 16);
				else
					value |= (pp->pull_bits << 16);
				AOPUD_WRITE_4(sc, pp->pull_addr, value);
			}
			/* FALLTHROUGH */
		case aml8726_disable_pm:
		case aml8726_enable_pm:
			if (pp->aobus == false) {
				value = PEN_READ_4(sc, pp->pull_addr);
				if (pm == aml8726_disable_pm)
					value &= ~pp->pull_bits;
				else
					value |= pp->pull_bits;
				PEN_WRITE_4(sc, pp->pull_addr, value);
			} else {
				value = AOPEN_READ_4(sc, pp->pull_addr);
				if (pm == aml8726_disable_pm)
					value &= ~pp->pull_bits;
				else
					value |= pp->pull_bits;
				AOPEN_WRITE_4(sc, pp->pull_addr, value);
			}
			break;
		default:
			break;
		}

		AML_PINCTRL_UNLOCK(sc);
	}

	OF_prop_free(pins);

	return (0);
}


static device_method_t aml8726_pinctrl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_pinctrl_probe),
	DEVMETHOD(device_attach,	aml8726_pinctrl_attach),
	DEVMETHOD(device_detach,	aml8726_pinctrl_detach),

	/* fdt_pinctrl interface */
	DEVMETHOD(fdt_pinctrl_configure,aml8726_pinctrl_configure_pins),

	DEVMETHOD_END
};

static driver_t aml8726_pinctrl_driver = {
	"pinctrl",
	aml8726_pinctrl_methods,
	sizeof(struct aml8726_pinctrl_softc),
};

static devclass_t aml8726_pinctrl_devclass;

EARLY_DRIVER_MODULE(pinctrl, simplebus, aml8726_pinctrl_driver,
    aml8726_pinctrl_devclass, 0, 0,  BUS_PASS_CPU + BUS_PASS_ORDER_LATE);
