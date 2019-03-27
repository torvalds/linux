/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2002 Benno Rice.
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>

#include <machine/resource.h>

#include <dev/ofw/openfirm.h>

#include <powerpc/powermac/maciovar.h>

struct pswitch_softc {
	int	sc_irqrid;
	struct	resource *sc_irq;
	void	*sc_ih;
};

static int	pswitch_probe(device_t);
static int	pswitch_attach(device_t);

static int	pswitch_intr(void *);

static device_method_t pswitch_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pswitch_probe),
	DEVMETHOD(device_attach,	pswitch_attach),

	{ 0, 0 }
};

static driver_t pswitch_driver = {
	"pswitch",
	pswitch_methods,
	sizeof(struct pswitch_softc)
};

static devclass_t pswitch_devclass;

DRIVER_MODULE(pswitch, macio, pswitch_driver, pswitch_devclass, 0, 0);

static int
pswitch_probe(device_t dev)
{
	char	*type = macio_get_devtype(dev);

	if (strcmp(type, "gpio") != 0)
		return (ENXIO);

	device_set_desc(dev, "GPIO Programmer's Switch");
	return (0);
}

static int
pswitch_attach(device_t dev)
{
	struct		pswitch_softc *sc;
	phandle_t	node, child;
	char		type[32];
	u_int		irq[2];

	sc = device_get_softc(dev);
	node = macio_get_node(dev);

	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		if (OF_getprop(child, "device_type", type, 32) == -1)
			continue;

		if (strcmp(type, "programmer-switch") == 0)
			break;
	}

	if (child == 0) {
		device_printf(dev, "could not find correct node\n");
		return (ENXIO);
	}

	if (OF_getprop(child, "interrupts", irq, sizeof(irq)) == -1) {
		device_printf(dev, "could not get interrupt\n");
		return (ENXIO);
	}

	sc->sc_irqrid = 0;
	sc->sc_irq = bus_alloc_resource(dev, SYS_RES_IRQ, &sc->sc_irqrid,
	    irq[0], irq[0], 1, RF_ACTIVE);
	if (sc->sc_irq == NULL) {
		device_printf(dev, "could not allocate interrupt\n");
		return (ENXIO);
	}

	if (bus_setup_intr(dev, sc->sc_irq, INTR_TYPE_MISC,
	    pswitch_intr, NULL, dev, &sc->sc_ih) != 0) {
		device_printf(dev, "could not setup interrupt\n");
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irqrid,
		    sc->sc_irq);
		return (ENXIO);
	}

	return (0);
}

static int
pswitch_intr(void *arg)
{
	device_t	dev;

	dev = (device_t)arg;

	kdb_enter(KDB_WHY_POWERPC, device_get_nameunit(dev));
	return (FILTER_HANDLED);
}
