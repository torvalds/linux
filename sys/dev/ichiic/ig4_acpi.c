/*-
 * Copyright (c) 2016 Oleksandr Tymoshenko <gonzo@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_acpi.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>
#include <dev/iicbus/iiconf.h>

#include <dev/ichiic/ig4_reg.h>
#include <dev/ichiic/ig4_var.h>

static int	ig4iic_acpi_probe(device_t dev);
static int	ig4iic_acpi_attach(device_t dev);
static int	ig4iic_acpi_detach(device_t dev);

static char *ig4iic_ids[] = {
	"INT33C2",
	"INT33C3",
	"INT3432",
	"INT3433",
	"80860F41",
	"808622C1",
	"AMDI0510",
	"AMDI0010",
	"APMC0D0F",
	NULL
};

static int
ig4iic_acpi_probe(device_t dev)
{
	ig4iic_softc_t *sc;
	char *hid;
	int rv;

	sc = device_get_softc(dev);
	if (acpi_disabled("ig4iic"))
		return (ENXIO);
	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, ig4iic_ids, &hid);
	if (rv > 0)
		return (rv);

        if (strcmp("AMDI0010", hid) == 0)
                sc->access_intr_mask = 1;
	device_set_desc(dev, "Designware I2C Controller");
	return (rv);
}

static int
ig4iic_acpi_attach(device_t dev)
{
	ig4iic_softc_t	*sc;
	int error;

	sc = device_get_softc(dev);

	sc->dev = dev;
	/* All the HIDs matched are Atom SOCs. */
	sc->version = IG4_ATOM;
	sc->regs_rid = 0;
	sc->regs_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
					  &sc->regs_rid, RF_ACTIVE);
	if (sc->regs_res == NULL) {
		device_printf(dev, "unable to map registers\n");
		ig4iic_acpi_detach(dev);
		return (ENXIO);
	}
	sc->intr_rid = 0;
	sc->intr_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
					  &sc->intr_rid, RF_SHAREABLE | RF_ACTIVE);
	if (sc->intr_res == NULL) {
		device_printf(dev, "unable to map interrupt\n");
		ig4iic_acpi_detach(dev);
		return (ENXIO);
	}
	sc->platform_attached = 1;

	error = ig4iic_attach(sc);
	if (error)
		ig4iic_acpi_detach(dev);

	return (error);
}

static int
ig4iic_acpi_detach(device_t dev)
{
	ig4iic_softc_t *sc = device_get_softc(dev);
	int error;

	if (sc->platform_attached) {
		error = ig4iic_detach(sc);
		if (error)
			return (error);
		sc->platform_attached = 0;
	}

	if (sc->intr_res) {
		bus_release_resource(dev, SYS_RES_IRQ,
				     sc->intr_rid, sc->intr_res);
		sc->intr_res = NULL;
	}
	if (sc->regs_res) {
		bus_release_resource(dev, SYS_RES_MEMORY,
				     sc->regs_rid, sc->regs_res);
		sc->regs_res = NULL;
	}

	return (0);
}

static device_method_t ig4iic_acpi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, ig4iic_acpi_probe),
	DEVMETHOD(device_attach, ig4iic_acpi_attach),
	DEVMETHOD(device_detach, ig4iic_acpi_detach),

	/* iicbus interface */
	DEVMETHOD(iicbus_transfer, ig4iic_transfer),
	DEVMETHOD(iicbus_reset, ig4iic_reset),
	DEVMETHOD(iicbus_callback, iicbus_null_callback),

	DEVMETHOD_END
};

static driver_t ig4iic_acpi_driver = {
	"ig4iic_acpi",
	ig4iic_acpi_methods,
	sizeof(struct ig4iic_softc),
};

static devclass_t ig4iic_acpi_devclass;
DRIVER_MODULE(ig4iic_acpi, acpi, ig4iic_acpi_driver, ig4iic_acpi_devclass, 0, 0);

MODULE_DEPEND(ig4iic_acpi, acpi, 1, 1, 1);
MODULE_DEPEND(ig4iic_acpi, pci, 1, 1, 1);
MODULE_DEPEND(ig4iic_acpi, iicbus, IICBUS_MINVER, IICBUS_PREFVER, IICBUS_MAXVER);
MODULE_VERSION(ig4iic_acpi, 1);
