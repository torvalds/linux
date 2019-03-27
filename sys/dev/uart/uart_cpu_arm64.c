/*-
 * Copyright (c) 2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under sponsorship from
 * the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt_acpi.h"
#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu.h>

#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/actables.h>
#include <dev/uart/uart_cpu_acpi.h>
#endif

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/uart/uart_cpu_fdt.h>
#endif

/*
 * UART console routines.
 */
extern struct bus_space memmap_bus;
bus_space_tag_t uart_bus_space_io;
bus_space_tag_t uart_bus_space_mem = &memmap_bus;

int
uart_cpu_eqres(struct uart_bas *b1, struct uart_bas *b2)
{

	if (pmap_kextract(b1->bsh) == 0)
		return (0);
	if (pmap_kextract(b2->bsh) == 0)
		return (0);
	return ((pmap_kextract(b1->bsh) == pmap_kextract(b2->bsh)) ? 1 : 0);
}

#ifdef DEV_ACPI
static struct acpi_uart_compat_data *
uart_cpu_acpi_scan(uint8_t interface_type)
{
	struct acpi_uart_compat_data **cd, *curcd;
	int i;

	SET_FOREACH(cd, uart_acpi_class_and_device_set) {
		curcd = *cd;
		for (i = 0; curcd[i].cd_hid != NULL; i++) {
			if (curcd[i].cd_port_subtype == interface_type)
				return (&curcd[i]);
		}
	}

	SET_FOREACH(cd, uart_acpi_class_set) {
		curcd = *cd;
		for (i = 0; curcd[i].cd_hid != NULL; i++) {
			if (curcd[i].cd_port_subtype == interface_type)
				return (&curcd[i]);
		}
	}

	return (NULL);
}

static int
uart_cpu_acpi_probe(struct uart_class **classp, bus_space_tag_t *bst,
    bus_space_handle_t *bsh, int *baud, u_int *rclk, u_int *shiftp,
    u_int *iowidthp)
{
	struct acpi_uart_compat_data *cd;
	ACPI_TABLE_SPCR *spcr;
	vm_paddr_t spcr_physaddr;
	int err;

	err = ENXIO;
	spcr_physaddr = acpi_find_table(ACPI_SIG_SPCR);
	if (spcr_physaddr == 0)
		return (ENXIO);

	spcr = acpi_map_table(spcr_physaddr, ACPI_SIG_SPCR);

	cd = uart_cpu_acpi_scan(spcr->InterfaceType);
	if (cd == NULL)
		goto out;

	switch(spcr->BaudRate) {
	case 3:
		*baud = 9600;
		break;
	case 4:
		*baud = 19200;
		break;
	case 6:
		*baud = 57600;
		break;
	case 7:
		*baud = 115200;
		break;
	default:
		goto out;
	}

	err = acpi_map_addr(&spcr->SerialPort, bst, bsh, PAGE_SIZE);
	if (err != 0)
		goto out;

	*classp = cd->cd_class;
	*rclk = 0;
	*shiftp = spcr->SerialPort.AccessWidth - 1;
	*iowidthp = spcr->SerialPort.BitWidth / 8;

out:
	acpi_unmap_table(spcr);
	return (err);
}
#endif

int
uart_cpu_getdev(int devtype, struct uart_devinfo *di)
{
	struct uart_class *class;
	bus_space_handle_t bsh;
	bus_space_tag_t bst;
	u_int rclk, shift, iowidth;
	int br, err;

	/* Allow overriding the FDT using the environment. */
	class = &uart_ns8250_class;
	err = uart_getenv(devtype, di, class);
	if (err == 0)
		return (0);

	if (devtype != UART_DEV_CONSOLE)
		return (ENXIO);

	err = ENXIO;
#ifdef DEV_ACPI
	err = uart_cpu_acpi_probe(&class, &bst, &bsh, &br, &rclk, &shift,
	    &iowidth);
#endif
#ifdef FDT
	if (err != 0) {
		err = uart_cpu_fdt_probe(&class, &bst, &bsh, &br, &rclk,
		    &shift, &iowidth);
	}
#endif
	if (err != 0)
		return (err);

	/*
	 * Finalize configuration.
	 */
	di->bas.chan = 0;
	di->bas.regshft = shift;
	di->bas.regiowidth = iowidth;
	di->baudrate = br;
	di->bas.rclk = rclk;
	di->ops = uart_getops(class);
	di->databits = 8;
	di->stopbits = 1;
	di->parity = UART_PARITY_NONE;
	di->bas.bst = bst;
	di->bas.bsh = bsh;
	uart_bus_space_mem = di->bas.bst;
	uart_bus_space_io = NULL;

	return (0);
}
