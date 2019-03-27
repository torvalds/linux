/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 M. Warner Losh.
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu_acpi.h>
#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <dev/acpica/acpivar.h>


static int uart_acpi_probe(device_t dev);

static device_method_t uart_acpi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		uart_acpi_probe),
	DEVMETHOD(device_attach,	uart_bus_attach),
	DEVMETHOD(device_detach,	uart_bus_detach),
	DEVMETHOD(device_resume,	uart_bus_resume),
	{ 0, 0 }
};

static driver_t uart_acpi_driver = {
	uart_driver_name,
	uart_acpi_methods,
	sizeof(struct uart_softc),
};

static struct acpi_uart_compat_data *
uart_acpi_find_device(device_t dev)
{
	struct acpi_uart_compat_data **cd, *cd_it;
	ACPI_HANDLE h;

	if ((h = acpi_get_handle(dev)) == NULL)
		return (NULL);

	SET_FOREACH(cd, uart_acpi_class_and_device_set) {
		for (cd_it = *cd; cd_it->cd_hid != NULL; cd_it++) {
			if (acpi_MatchHid(h, cd_it->cd_hid))
				return (cd_it);
		}
	}

	return (NULL);
}

static int
uart_acpi_probe(device_t dev)
{
	struct uart_softc *sc;
	struct acpi_uart_compat_data *cd;

	sc = device_get_softc(dev);

	if ((cd = uart_acpi_find_device(dev)) != NULL) {
		sc->sc_class = cd->cd_class;
		if (cd->cd_desc != NULL)
			device_set_desc(dev, cd->cd_desc);
		return (uart_bus_probe(dev, cd->cd_regshft, cd->cd_regiowidth,
		    cd->cd_rclk, 0, 0, cd->cd_quirks));
	}
	return (ENXIO);
}

DRIVER_MODULE(uart, acpi, uart_acpi_driver, uart_devclass, 0, 0);
