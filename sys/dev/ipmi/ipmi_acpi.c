/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 IronPort Systems Inc. <ambrisko@ironport.com>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/selinfo.h>

#include <contrib/dev/acpica/include/acpi.h>

#include <dev/acpica/acpivar.h>

/* Hooks for the ACPI CA debugging infrastructure */
#define _COMPONENT	ACPI_BUTTON
ACPI_MODULE_NAME("IPMI")

#ifdef LOCAL_MODULE
#include <ipmi.h>
#include <ipmivars.h>
#else
#include <sys/ipmi.h>
#include <dev/ipmi/ipmivars.h>
#endif

static int ipmi_acpi_probe(device_t);
static int ipmi_acpi_attach(device_t);

int
ipmi_acpi_probe(device_t dev)
{
	static char *ipmi_ids[] = {"IPI0001", NULL};
	int rv;

	if (ipmi_attached)
		return (EBUSY);

	if (acpi_disabled("ipmi"))
		return (ENXIO);
	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, ipmi_ids, NULL);
	if (rv <= 0)
		device_set_desc(dev, "IPMI System Interface");

	return (rv);
}

static int
ipmi_acpi_attach(device_t dev)
{
	ACPI_HANDLE devh;
	const char *mode;
	struct ipmi_get_info info;
	struct ipmi_softc *sc = device_get_softc(dev);
	int count, error, flags, i, type;
	int interface_type = 0, interface_version = 0;

	error = 0;
	devh = acpi_get_handle(dev);
	if (ACPI_FAILURE(acpi_GetInteger(devh, "_IFT", &interface_type)))
		return (ENXIO);

	if (ACPI_FAILURE(acpi_GetInteger(devh, "_SRV", &interface_version)))
		return (ENXIO);

	switch (interface_type) {
	case KCS_MODE:
		count = 2;
		mode = "KCS";
		break;
	case SMIC_MODE:
		count = 3;
		mode = "SMIC";
		break;
	case BT_MODE:
		device_printf(dev, "BT interface not supported\n");
		return (ENXIO);
	case SSIF_MODE:
		if (ACPI_FAILURE(acpi_GetInteger(devh, "_ADR", &flags)))
			return (ENXIO);
		info.address = flags;
		device_printf(dev, "SSIF interface not supported on ACPI\n");
		return (0);
	default:
		return (ENXIO);
	}

	if (bus_get_resource(dev, SYS_RES_IOPORT, 0, NULL, NULL) == 0)
		type = SYS_RES_IOPORT;
	else if (bus_get_resource(dev, SYS_RES_MEMORY, 0, NULL, NULL) == 0)
		type = SYS_RES_MEMORY;
	else {
		device_printf(dev, "unknown resource type\n");
		return (ENXIO);
	}

	sc->ipmi_io_rid = 0;
	sc->ipmi_io_res[0] = bus_alloc_resource_any(dev, type,
	    &sc->ipmi_io_rid, RF_ACTIVE);
	sc->ipmi_io_type = type;
	sc->ipmi_io_spacing = 1;
	if (sc->ipmi_io_res[0] == NULL) {
		device_printf(dev, "couldn't configure I/O resource\n");
		return (ENXIO);
	}

	/* If we have multiple resources, allocate up to MAX_RES. */
	for (i = 1; i < MAX_RES; i++) {
		sc->ipmi_io_rid = i;
		sc->ipmi_io_res[i] = bus_alloc_resource_any(dev, type,
		    &sc->ipmi_io_rid, RF_ACTIVE);
		if (sc->ipmi_io_res[i] == NULL)
			break;
	}
	sc->ipmi_io_rid = 0;

	/* If we have multiple resources, make sure we have enough of them. */
	if (sc->ipmi_io_res[1] != NULL && sc->ipmi_io_res[count - 1] == NULL) {
		device_printf(dev, "too few I/O resources\n");
		error = ENXIO;
		goto bad;
	}

	device_printf(dev, "%s mode found at %s 0x%jx on %s\n",
	    mode, type == SYS_RES_IOPORT ? "io" : "mem",
	    (uintmax_t)rman_get_start(sc->ipmi_io_res[0]),
	    device_get_name(device_get_parent(dev)));

	sc->ipmi_dev = dev;

	/*
	 * Setup an interrupt if we have an interrupt resource.  We
	 * don't support GPE interrupts via _GPE yet.
	 */
	sc->ipmi_irq_rid = 0;
	sc->ipmi_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->ipmi_irq_rid, RF_SHAREABLE | RF_ACTIVE);

	/* Warn if _GPE exists. */
	if (ACPI_SUCCESS(AcpiEvaluateObject(devh, "_GPE", NULL, NULL)))
		device_printf(dev, "_GPE support not implemented\n");

	/*
	 * We assume an alignment of 1 byte as currently the IPMI spec
	 * doesn't provide any way to determine the alignment via ACPI.
	 */
	switch (interface_type) {
	case KCS_MODE:
		error = ipmi_kcs_attach(sc);
		if (error)
			goto bad;
		break;
	case SMIC_MODE:
		error = ipmi_smic_attach(sc);
		if (error)
			goto bad;
		break;
	}
	error = ipmi_attach(dev);
	if (error)
		goto bad;

	return (0);
bad:
	ipmi_release_resources(dev);
	return (error);
}

static device_method_t ipmi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ipmi_acpi_probe),
	DEVMETHOD(device_attach,	ipmi_acpi_attach),
	DEVMETHOD(device_detach,	ipmi_detach),
	{ 0, 0 }
};

static driver_t ipmi_acpi_driver = {
	"ipmi",
	ipmi_methods,
	sizeof(struct ipmi_softc),
};

DRIVER_MODULE(ipmi_acpi, acpi, ipmi_acpi_driver, ipmi_devclass, 0, 0);
MODULE_DEPEND(ipmi_acpi, acpi, 1, 1, 1);
