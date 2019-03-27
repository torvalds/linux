/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_kbd.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <machine/resource.h>
#include <sys/rman.h>
#include <machine/bus.h>

#include <dev/atkbdc/atkbdc_subr.h>
#include <dev/atkbdc/atkbdcreg.h>

#include <isa/isareg.h>
#include <isa/isavar.h>

static int	atkbdc_isa_probe(device_t dev);
static int	atkbdc_isa_attach(device_t dev);
static device_t	atkbdc_isa_add_child(device_t bus, u_int order, const char *name,
		    int unit);
static struct resource *atkbdc_isa_alloc_resource(device_t dev, device_t child,
		    int type, int *rid, rman_res_t start, rman_res_t end,
		    rman_res_t count, u_int flags);
static int	atkbdc_isa_release_resource(device_t dev, device_t child,
		    int type, int rid, struct resource *r);

static device_method_t atkbdc_isa_methods[] = {
	DEVMETHOD(device_probe,		atkbdc_isa_probe),
	DEVMETHOD(device_attach,	atkbdc_isa_attach),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	DEVMETHOD(bus_add_child,	atkbdc_isa_add_child),
	DEVMETHOD(bus_print_child,	atkbdc_print_child),
	DEVMETHOD(bus_read_ivar,	atkbdc_read_ivar),
	DEVMETHOD(bus_write_ivar,	atkbdc_write_ivar),
	DEVMETHOD(bus_get_resource_list,atkbdc_get_resource_list),
	DEVMETHOD(bus_alloc_resource,	atkbdc_isa_alloc_resource),
	DEVMETHOD(bus_release_resource,	atkbdc_isa_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_get_resource,	bus_generic_rl_get_resource),
	DEVMETHOD(bus_set_resource,	bus_generic_rl_set_resource),
	DEVMETHOD(bus_delete_resource,	bus_generic_rl_delete_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	{ 0, 0 }
};

static driver_t atkbdc_isa_driver = {
	ATKBDC_DRIVER_NAME,
	atkbdc_isa_methods,
	sizeof(atkbdc_softc_t *),
};

static struct isa_pnp_id atkbdc_ids[] = {
	{ 0x0303d041, "Keyboard controller (i8042)" },	/* PNP0303 */
	{ 0x0b03d041, "Keyboard controller (i8042)" },	/* PNP030B */
	{ 0x2003d041, "Keyboard controller (i8042)" },	/* PNP0320 */
	{ 0 }
};

static int
atkbdc_isa_probe(device_t dev)
{
	struct resource	*port0;
	struct resource	*port1;
	rman_res_t	start;
	rman_res_t	count;
	int		error;
	int		rid;
#if defined(__i386__) || defined(__amd64__)
	bus_space_tag_t	tag;
	bus_space_handle_t ioh1;
	volatile int	i;
	register_t	flags;
#endif

	/* check PnP IDs */
	if (ISA_PNP_PROBE(device_get_parent(dev), dev, atkbdc_ids) == ENXIO)
		return ENXIO;

	device_set_desc(dev, "Keyboard controller (i8042)");

	/*
	 * Adjust I/O port resources.
	 * The AT keyboard controller uses two ports (a command/data port
	 * 0x60 and a status port 0x64), which may be given to us in 
	 * one resource (0x60 through 0x64) or as two separate resources
	 * (0x60 and 0x64). Some brain-damaged ACPI BIOS has reversed
	 * command/data port and status port. Furthermore, /boot/device.hints
	 * may contain just one port, 0x60. We shall adjust resource settings
	 * so that these two ports are available as two separate resources
	 * in correct order.
	 */
	device_quiet(dev);
	rid = 0;
	if (bus_get_resource(dev, SYS_RES_IOPORT, rid, &start, &count) != 0)
		return ENXIO;
	if (start == IO_KBD + KBD_STATUS_PORT) {
		start = IO_KBD;
		count++;
	}
	if (count > 1)	/* adjust the count and/or start port */
		bus_set_resource(dev, SYS_RES_IOPORT, rid, start, 1);
	port0 = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid, RF_ACTIVE);
	if (port0 == NULL)
		return ENXIO;
	rid = 1;
	if (bus_get_resource(dev, SYS_RES_IOPORT, rid, NULL, NULL) != 0)
		bus_set_resource(dev, SYS_RES_IOPORT, 1,
				 start + KBD_STATUS_PORT, 1);
	port1 = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid, RF_ACTIVE);
	if (port1 == NULL) {
		bus_release_resource(dev, SYS_RES_IOPORT, 0, port0);
		return ENXIO;
	}

#if defined(__i386__) || defined(__amd64__)
	/*
	 * Check if we really have AT keyboard controller. Poll status
	 * register until we get "all clear" indication. If no such
	 * indication comes, it probably means that there is no AT
	 * keyboard controller present. Give up in such case. Check relies
	 * on the fact that reading from non-existing in/out port returns
	 * 0xff on i386. May or may not be true on other platforms.
	 */
	tag = rman_get_bustag(port0);
	ioh1 = rman_get_bushandle(port1);
	flags = intr_disable();
	for (i = 0; i != 65535; i++) {
		if ((bus_space_read_1(tag, ioh1, 0) & 0x2) == 0)
			break;
	}
	intr_restore(flags);
	if (i == 65535) {
		bus_release_resource(dev, SYS_RES_IOPORT, 0, port0);
		bus_release_resource(dev, SYS_RES_IOPORT, 1, port1);
		if (bootverbose)
			device_printf(dev, "AT keyboard controller not found\n");
		return ENXIO;
	}
#endif

	device_verbose(dev);

	error = atkbdc_probe_unit(device_get_unit(dev), port0, port1);

	bus_release_resource(dev, SYS_RES_IOPORT, 0, port0);
	bus_release_resource(dev, SYS_RES_IOPORT, 1, port1);

	return error;
}

static int
atkbdc_isa_attach(device_t dev)
{
	atkbdc_softc_t	*sc;
	int		unit;
	int		error;
	int		rid;

	unit = device_get_unit(dev);
	sc = *(atkbdc_softc_t **)device_get_softc(dev);
	if (sc == NULL) {
		/*
		 * We have to maintain two copies of the kbdc_softc struct,
		 * as the low-level console needs to have access to the
		 * keyboard controller before kbdc is probed and attached.
		 * kbdc_soft[] contains the default entry for that purpose.
		 * See atkbdc.c. XXX
		 */
		sc = atkbdc_get_softc(unit);
		if (sc == NULL)
			return ENOMEM;
	}

	rid = 0;
	sc->retry = 5000;
	sc->port0 = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
					   RF_ACTIVE);
	if (sc->port0 == NULL)
		return ENXIO;
	rid = 1;
	sc->port1 = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
					   RF_ACTIVE);
	if (sc->port1 == NULL) {
		bus_release_resource(dev, SYS_RES_IOPORT, 0, sc->port0);
		return ENXIO;
	}

	/*
	 * If the device is not created by the PnP BIOS or ACPI, then
	 * the hint for the IRQ is on the child atkbd device, not the
	 * keyboard controller, so this can fail.
	 */
	rid = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);

	error = atkbdc_attach_unit(unit, sc, sc->port0, sc->port1);
	if (error) {
		bus_release_resource(dev, SYS_RES_IOPORT, 0, sc->port0);
		bus_release_resource(dev, SYS_RES_IOPORT, 1, sc->port1);
		if (sc->irq != NULL)
			bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq);
		return error;
	}
	*(atkbdc_softc_t **)device_get_softc(dev) = sc;

	bus_generic_probe(dev);
	bus_generic_attach(dev);

	return 0;
}

static device_t
atkbdc_isa_add_child(device_t bus, u_int order, const char *name, int unit)
{
	atkbdc_device_t	*ivar;
	atkbdc_softc_t	*sc;
	device_t	child;
	int		t;

	sc = *(atkbdc_softc_t **)device_get_softc(bus);
	ivar = malloc(sizeof(struct atkbdc_device), M_ATKBDDEV,
		M_NOWAIT | M_ZERO);
	if (!ivar)
		return NULL;

	child = device_add_child_ordered(bus, order, name, unit);
	if (child == NULL) {
		free(ivar, M_ATKBDDEV);
		return child;
	}

	resource_list_init(&ivar->resources);
	ivar->rid = order;

	/*
	 * If the device is not created by the PnP BIOS or ACPI, refer
	 * to device hints for IRQ.  We always populate the resource
	 * list entry so we can use a standard bus_get_resource()
	 * method.
	 */
	if (order == KBDC_RID_KBD) {
		if (sc->irq == NULL) {
			if (resource_int_value(name, unit, "irq", &t) != 0)
				t = -1;
		} else
			t = rman_get_start(sc->irq);
		if (t > 0)
			resource_list_add(&ivar->resources, SYS_RES_IRQ,
			    ivar->rid, t, t, 1);
	}

	if (resource_disabled(name, unit))
		device_disable(child);

	device_set_ivars(child, ivar);

	return child;
}

struct resource *
atkbdc_isa_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	atkbdc_softc_t	*sc;
	
	sc = *(atkbdc_softc_t **)device_get_softc(dev);
	if (type == SYS_RES_IRQ && *rid == KBDC_RID_KBD && sc->irq != NULL)
		return (sc->irq);
	return (bus_generic_rl_alloc_resource(dev, child, type, rid, start,
	    end, count, flags));
}

static int
atkbdc_isa_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r)
{
	atkbdc_softc_t	*sc;
	
	sc = *(atkbdc_softc_t **)device_get_softc(dev);
	if (type == SYS_RES_IRQ && rid == KBDC_RID_KBD && r == sc->irq)
		return (0);
	return (bus_generic_rl_release_resource(dev, child, type, rid, r));
}

DRIVER_MODULE(atkbdc, isa, atkbdc_isa_driver, atkbdc_devclass, 0, 0);
DRIVER_MODULE(atkbdc, acpi, atkbdc_isa_driver, atkbdc_devclass, 0, 0);
ISA_PNP_INFO(atkbdc_ids);
