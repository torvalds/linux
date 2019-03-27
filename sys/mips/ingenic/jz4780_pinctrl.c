/*-
 * Copyright 2015 Alexander Kabaev <kan@FreeBSD.org>
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
 * Ingenic JZ4780 pinctrl driver.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/fdt/fdt_common.h>
#include <dev/fdt/fdt_pinctrl.h>
#include <dev/fdt/simplebus.h>

#include <mips/ingenic/jz4780_regs.h>

#include "jz4780_gpio_if.h"

struct jz4780_pinctrl_softc {
	struct simplebus_softc          ssc;
	device_t			dev;
};

#define CHIP_REG_STRIDE			256
#define CHIP_REG_OFFSET(base, chip)	((base) + (chip) * CHIP_REG_STRIDE)

static int
jz4780_pinctrl_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ingenic,jz4780-pinctrl"))
		return (ENXIO);

	device_set_desc(dev, "Ingenic JZ4780 GPIO");

	return (BUS_PROBE_DEFAULT);
}

static int
jz4780_pinctrl_attach(device_t dev)
{
	struct jz4780_pinctrl_softc *sc;
	struct resource_list *rs;
	struct resource_list_entry *re;
	phandle_t dt_parent, dt_child;
	int i, ret;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/*
	 * Fetch our own resource list to dole memory between children
	 */
	rs = BUS_GET_RESOURCE_LIST(device_get_parent(dev), dev);
	if (rs == NULL)
		return (ENXIO);
	re = resource_list_find(rs, SYS_RES_MEMORY, 0);
	if (re == NULL)
		return (ENXIO);

	simplebus_init(dev, 0);

	/* Iterate over this node children, looking for pin controllers */
	dt_parent = ofw_bus_get_node(dev);
	i = 0;
	for (dt_child = OF_child(dt_parent); dt_child != 0;
	    dt_child = OF_peer(dt_child)) {
		struct simplebus_devinfo *ndi;
		device_t child;
		bus_addr_t phys;
		bus_size_t size;

		/* Add gpio controller child */
		if (!OF_hasprop(dt_child, "gpio-controller"))
			continue;
		child = simplebus_add_device(dev, dt_child, 0,  NULL, -1, NULL);
		if (child == NULL)
			break;
		/* Setup child resources */
		phys = CHIP_REG_OFFSET(re->start, i);
		size = CHIP_REG_STRIDE;
		if (phys + size - 1 <= re->end) {
			ndi = device_get_ivars(child);
			resource_list_add(&ndi->rl, SYS_RES_MEMORY, 0,
			    phys, phys + size - 1, size);
		}
		i++;
	}

	ret = bus_generic_attach(dev);
	if (ret == 0) {
	    fdt_pinctrl_register(dev, "ingenic,pins");
	    fdt_pinctrl_configure_tree(dev);
	}
	return (ret);
}

static int
jz4780_pinctrl_detach(device_t dev)
{

	bus_generic_detach(dev);
	return (0);
}

struct jx4780_bias_prop {
	const char *name;
	uint32_t    bias;
};

static struct jx4780_bias_prop jx4780_bias_table[] = {
	{ "bias-disable", 0 },
	{ "bias-pull-up", GPIO_PIN_PULLUP },
	{ "bias-pull-down", GPIO_PIN_PULLDOWN },
};

static int
jz4780_pinctrl_parse_pincfg(phandle_t pincfgxref, uint32_t *bias_value)
{
	phandle_t pincfg_node;
	int i;

	pincfg_node = OF_node_from_xref(pincfgxref);
	for (i = 0; i < nitems(jx4780_bias_table); i++) {
		if (OF_hasprop(pincfg_node, jx4780_bias_table[i].name)) {
			*bias_value = jx4780_bias_table[i].bias;
			return 0;
		}
	}

	return -1;
}

static device_t
jz4780_pinctrl_chip_lookup(struct jz4780_pinctrl_softc *sc, phandle_t chipxref)
{
	device_t chipdev;

	chipdev = OF_device_from_xref(chipxref);
	return chipdev;
}

static int
jz4780_pinctrl_configure_pins(device_t dev, phandle_t cfgxref)
{
	struct jz4780_pinctrl_softc *sc = device_get_softc(dev);
	device_t  chip;
	phandle_t node;
	ssize_t i, len;
	uint32_t *value, *pconf;
	int result;

	node = OF_node_from_xref(cfgxref);

	len = OF_getencprop_alloc_multi(node, "ingenic,pins",
	    sizeof(uint32_t) * 4, (void **)&value);
	if (len < 0) {
		device_printf(dev,
		    "missing ingenic,pins attribute in FDT\n");
		return (ENXIO);
	}

	pconf = value;
	result = EINVAL;
	for (i = 0; i < len; i++, pconf += 4) {
		uint32_t bias;

		/* Lookup the chip that handles this configuration */
		chip = jz4780_pinctrl_chip_lookup(sc, pconf[0]);
		if (chip == NULL) {
			device_printf(dev,
			    "invalid gpio controller reference in FDT\n");
			goto done;
		}

		if (jz4780_pinctrl_parse_pincfg(pconf[3], &bias) != 0) {
			device_printf(dev,
			    "invalid pin bias for pin %u on %s in FDT\n",
			    pconf[1], ofw_bus_get_name(chip));
			goto done;
		}

		result = JZ4780_GPIO_CONFIGURE_PIN(chip, pconf[1], pconf[2],
		    bias);
		if (result != 0) {
			device_printf(dev,
			    "failed to configure pin %u on %s\n", pconf[1],
			    ofw_bus_get_name(chip));
			goto done;
		}
	}

	result = 0;
done:
	free(value, M_OFWPROP);
	return (result);
}


static device_method_t jz4780_pinctrl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		jz4780_pinctrl_probe),
	DEVMETHOD(device_attach,	jz4780_pinctrl_attach),
	DEVMETHOD(device_detach,	jz4780_pinctrl_detach),

	/* fdt_pinctrl interface */
	DEVMETHOD(fdt_pinctrl_configure, jz4780_pinctrl_configure_pins),

	DEVMETHOD_END
};

static devclass_t jz4780_pinctrl_devclass;
DEFINE_CLASS_1(pinctrl, jz4780_pinctrl_driver, jz4780_pinctrl_methods,
            sizeof(struct jz4780_pinctrl_softc), simplebus_driver);
EARLY_DRIVER_MODULE(pinctrl, simplebus, jz4780_pinctrl_driver,
    jz4780_pinctrl_devclass, 0, 0, BUS_PASS_INTERRUPT + BUS_PASS_ORDER_LATE);
