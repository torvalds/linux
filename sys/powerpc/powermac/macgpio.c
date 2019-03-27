/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2008 by Nathan Whitehorn. All rights reserved.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Driver for MacIO GPIO controller
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/resource.h>
#include <machine/vmparam.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <powerpc/powermac/macgpiovar.h>

/*
 * Macgpio softc
 */
struct macgpio_softc {
	phandle_t	sc_node;
	struct resource	*sc_gpios;
	int		sc_gpios_rid;
	uint32_t	sc_saved_gpio_levels[2];
	uint32_t	sc_saved_gpios[GPIO_COUNT];
	uint32_t	sc_saved_extint_gpios[GPIO_EXTINT_COUNT];
};

static MALLOC_DEFINE(M_MACGPIO, "macgpio", "macgpio device information");

static int	macgpio_probe(device_t);
static int	macgpio_attach(device_t);
static int	macgpio_print_child(device_t dev, device_t child);
static void	macgpio_probe_nomatch(device_t, device_t);
static struct resource *macgpio_alloc_resource(device_t, device_t, int, int *,
		    rman_res_t, rman_res_t, rman_res_t, u_int);
static int	macgpio_activate_resource(device_t, device_t, int, int,
		    struct resource *);
static int	macgpio_deactivate_resource(device_t, device_t, int, int,
		    struct resource *);
static ofw_bus_get_devinfo_t macgpio_get_devinfo;
static int	macgpio_suspend(device_t dev);
static int	macgpio_resume(device_t dev);

/*
 * Bus interface definition
 */
static device_method_t macgpio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         macgpio_probe),
	DEVMETHOD(device_attach,        macgpio_attach),
	DEVMETHOD(device_detach,        bus_generic_detach),
	DEVMETHOD(device_shutdown,      bus_generic_shutdown),
	DEVMETHOD(device_suspend,       macgpio_suspend),
	DEVMETHOD(device_resume,        macgpio_resume),
	
	/* Bus interface */
	DEVMETHOD(bus_print_child,      macgpio_print_child),
	DEVMETHOD(bus_probe_nomatch,    macgpio_probe_nomatch),
	DEVMETHOD(bus_setup_intr,       bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,    bus_generic_teardown_intr),	

        DEVMETHOD(bus_alloc_resource,   macgpio_alloc_resource),
        DEVMETHOD(bus_activate_resource, macgpio_activate_resource),
        DEVMETHOD(bus_deactivate_resource, macgpio_deactivate_resource),
        DEVMETHOD(bus_release_resource, bus_generic_release_resource),

	DEVMETHOD(bus_child_pnpinfo_str, ofw_bus_gen_child_pnpinfo_str),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	macgpio_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	{ 0, 0 }
};

static driver_t macgpio_pci_driver = {
        "macgpio",
        macgpio_methods,
	sizeof(struct macgpio_softc)
};

devclass_t macgpio_devclass;

EARLY_DRIVER_MODULE(macgpio, macio, macgpio_pci_driver, macgpio_devclass, 0, 0,
    BUS_PASS_BUS);

struct macgpio_devinfo {
	struct ofw_bus_devinfo mdi_obdinfo;
	struct resource_list mdi_resources;

	int gpio_num;
};

static int
macgpio_probe(device_t dev)
{
	const char *name;

	name = ofw_bus_get_name(dev);
	if (name && strcmp(name, "gpio") == 0) {
		device_set_desc(dev, "MacIO GPIO Controller");
		return (0);
	}
	
        return (ENXIO);	
}

/*
 * Scan Open Firmware child nodes, and attach these as children
 * of the macgpio bus
 */
static int 
macgpio_attach(device_t dev)
{
	struct macgpio_softc *sc;
        struct macgpio_devinfo *dinfo;
        phandle_t root, child, iparent;
        device_t cdev;
	uint32_t irq[2];

	sc = device_get_softc(dev);
	root = sc->sc_node = ofw_bus_get_node(dev);
	
	sc->sc_gpios = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_gpios_rid, RF_ACTIVE);

	/*
	 * Iterate through the sub-devices
	 */
	for (child = OF_child(root); child != 0; child = OF_peer(child)) {
		dinfo = malloc(sizeof(*dinfo), M_MACGPIO, M_WAITOK | M_ZERO);
		if (ofw_bus_gen_setup_devinfo(&dinfo->mdi_obdinfo, child) !=
		    0) {
			free(dinfo, M_MACGPIO);
			continue;
		}

		if (OF_getencprop(child, "reg", &dinfo->gpio_num,
		    sizeof(dinfo->gpio_num)) != sizeof(dinfo->gpio_num)) {
			/*
			 * Some early GPIO controllers don't provide GPIO
			 * numbers for GPIOs designed only to provide
			 * interrupt resources.  We should still allow these
			 * to attach, but with caution.
			 */

			dinfo->gpio_num = -1;
		}

		resource_list_init(&dinfo->mdi_resources);

		if (OF_getencprop(child, "interrupts", irq, sizeof(irq)) == 
		    sizeof(irq)) {
			OF_searchencprop(child, "interrupt-parent", &iparent,
			    sizeof(iparent));
			resource_list_add(&dinfo->mdi_resources, SYS_RES_IRQ,
			    0, MAP_IRQ(iparent, irq[0]),
			    MAP_IRQ(iparent, irq[0]), 1);
		}

		/* Fix messed-up offsets */
		if (dinfo->gpio_num > 0x50)
			dinfo->gpio_num -= 0x50;

		cdev = device_add_child(dev, NULL, -1);
		if (cdev == NULL) {
			device_printf(dev, "<%s>: device_add_child failed\n",
			    dinfo->mdi_obdinfo.obd_name);
			ofw_bus_gen_destroy_devinfo(&dinfo->mdi_obdinfo);
			free(dinfo, M_MACGPIO);
			continue;
		}
		device_set_ivars(cdev, dinfo);
	}

	return (bus_generic_attach(dev));
}


static int
macgpio_print_child(device_t dev, device_t child)
{
        struct macgpio_devinfo *dinfo;
        int retval = 0;

        dinfo = device_get_ivars(child);

        retval += bus_print_child_header(dev, child);
	
	if (dinfo->gpio_num >= GPIO_BASE)
		printf(" gpio %d", dinfo->gpio_num - GPIO_BASE);
	else if (dinfo->gpio_num >= GPIO_EXTINT_BASE)
		printf(" extint-gpio %d", dinfo->gpio_num - GPIO_EXTINT_BASE);
	else if (dinfo->gpio_num >= 0)
		printf(" addr 0x%02x", dinfo->gpio_num); /* should not happen */

	resource_list_print_type(&dinfo->mdi_resources, "irq", SYS_RES_IRQ, 
	    "%jd");
        retval += bus_print_child_footer(dev, child);

        return (retval);
}


static void
macgpio_probe_nomatch(device_t dev, device_t child)
{
        struct macgpio_devinfo *dinfo;
	const char *type;

	if (bootverbose) {
		dinfo = device_get_ivars(child);

		if ((type = ofw_bus_get_type(child)) == NULL)
			type = "(unknown)";
		device_printf(dev, "<%s, %s>", type, ofw_bus_get_name(child));
		if (dinfo->gpio_num >= 0)
			printf(" gpio %d",dinfo->gpio_num);
		resource_list_print_type(&dinfo->mdi_resources, "irq", 
		    SYS_RES_IRQ, "%jd");
		printf(" (no driver attached)\n");
	}
}


static struct resource *
macgpio_alloc_resource(device_t bus, device_t child, int type, int *rid,
		     rman_res_t start, rman_res_t end, rman_res_t count,
		     u_int flags)
{
	struct macgpio_devinfo *dinfo;

	dinfo = device_get_ivars(child);

	if (type != SYS_RES_IRQ)
		return (NULL);

	return (resource_list_alloc(&dinfo->mdi_resources, bus, child, type, 
	    rid, start, end, count, flags));
}

static int
macgpio_activate_resource(device_t bus, device_t child, int type, int rid,
			   struct resource *res)
{
	struct macgpio_softc *sc;
	struct macgpio_devinfo *dinfo;
	u_char val;

	sc = device_get_softc(bus);
	dinfo = device_get_ivars(child);

	if (type != SYS_RES_IRQ)
		return ENXIO;

	if (dinfo->gpio_num >= 0) {
		val = bus_read_1(sc->sc_gpios,dinfo->gpio_num);
		val |= 0x80;
		bus_write_1(sc->sc_gpios,dinfo->gpio_num,val);
	}

	return (bus_activate_resource(bus, type, rid, res));
}


static int
macgpio_deactivate_resource(device_t bus, device_t child, int type, int rid,
			  struct resource *res)
{
	struct macgpio_softc *sc;
	struct macgpio_devinfo *dinfo;
	u_char val;

	sc = device_get_softc(bus);
	dinfo = device_get_ivars(child);

	if (type != SYS_RES_IRQ)
		return ENXIO;

	if (dinfo->gpio_num >= 0) {
		val = bus_read_1(sc->sc_gpios,dinfo->gpio_num);
		val &= ~0x80;
		bus_write_1(sc->sc_gpios,dinfo->gpio_num,val);
	}

	return (bus_deactivate_resource(bus, type, rid, res));
}

uint8_t
macgpio_read(device_t dev)
{
	struct macgpio_softc *sc;
	struct macgpio_devinfo *dinfo;

	sc = device_get_softc(device_get_parent(dev));
	dinfo = device_get_ivars(dev);

	if (dinfo->gpio_num < 0)
		return (0);

	return (bus_read_1(sc->sc_gpios,dinfo->gpio_num));
}

void
macgpio_write(device_t dev, uint8_t val)
{
	struct macgpio_softc *sc;
	struct macgpio_devinfo *dinfo;

	sc = device_get_softc(device_get_parent(dev));
	dinfo = device_get_ivars(dev);

	if (dinfo->gpio_num < 0)
		return;

	bus_write_1(sc->sc_gpios,dinfo->gpio_num,val);
}

static const struct ofw_bus_devinfo *
macgpio_get_devinfo(device_t dev, device_t child)
{
	struct macgpio_devinfo *dinfo;

	dinfo = device_get_ivars(child);
	return (&dinfo->mdi_obdinfo);
}

static int
macgpio_suspend(device_t dev)
{
	struct macgpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	sc->sc_saved_gpio_levels[0] = bus_read_4(sc->sc_gpios, GPIO_LEVELS_0);
	sc->sc_saved_gpio_levels[1] = bus_read_4(sc->sc_gpios, GPIO_LEVELS_1);

	for (i = 0; i < GPIO_COUNT; i++)
		sc->sc_saved_gpios[i] = bus_read_1(sc->sc_gpios, GPIO_BASE + i);
	for (i = 0; i < GPIO_EXTINT_COUNT; i++)
		sc->sc_saved_extint_gpios[i] = bus_read_1(sc->sc_gpios, GPIO_EXTINT_BASE + i);

	return (0);
}

static int
macgpio_resume(device_t dev)
{
	struct macgpio_softc *sc;
	int i;

	sc = device_get_softc(dev);
	bus_write_4(sc->sc_gpios, GPIO_LEVELS_0, sc->sc_saved_gpio_levels[0]);
	bus_write_4(sc->sc_gpios, GPIO_LEVELS_1, sc->sc_saved_gpio_levels[1]);

	for (i = 0; i < GPIO_COUNT; i++)
		bus_write_1(sc->sc_gpios, GPIO_BASE + i, sc->sc_saved_gpios[i]);
	for (i = 0; i < GPIO_EXTINT_COUNT; i++)
		bus_write_1(sc->sc_gpios, GPIO_EXTINT_BASE + i, sc->sc_saved_extint_gpios[i]);

	return (0);
}
