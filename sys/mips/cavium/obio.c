/*	$NetBSD: obio.c,v 1.11 2003/07/15 00:25:05 lukem Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001, 2002, 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
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
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * On-board device autoconfiguration support for Cavium OCTEON 1 family of
 * SoC devices.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <mips/cavium/octeon_pcmap_regs.h>
#include <mips/cavium/obiovar.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <mips/cavium/octeon_irq.h>

extern struct bus_space octeon_uart_tag;

static void	obio_identify(driver_t *, device_t);
static int	obio_probe(device_t);
static int	obio_attach(device_t);

static void
obio_identify(driver_t *drv, device_t parent)
{
	BUS_ADD_CHILD(parent, 0, "obio", 0);
}

static int
obio_probe(device_t dev)
{
	if (device_get_unit(dev) != 0)
		return (ENXIO);
	return (0);
}

static int
obio_attach(device_t dev)
{
	struct obio_softc *sc = device_get_softc(dev);

	sc->oba_st = mips_bus_space_generic;
	/*
	 * XXX
	 * Here and elsewhere using RBR as a base address because it kind of
	 * is, but that feels pretty sloppy.  Should consider adding a define
	 * that's more semantic, at least.
	 */
	sc->oba_addr = CVMX_MIO_UARTX_RBR(0);
	sc->oba_size = 0x10000;
	sc->oba_rman.rm_type = RMAN_ARRAY;
	sc->oba_rman.rm_descr = "OBIO I/O";
	if (rman_init(&sc->oba_rman) != 0 ||
	    rman_manage_region(&sc->oba_rman,
	    sc->oba_addr, sc->oba_addr + sc->oba_size) != 0)
		panic("obio_attach: failed to set up I/O rman");
	sc->oba_irq_rman.rm_type = RMAN_ARRAY;
	sc->oba_irq_rman.rm_descr = "OBIO IRQ";

	/* 
	 * This module is intended for UART purposes only and
	 * manages IRQs for UART0 and UART1.
	 */
	if (rman_init(&sc->oba_irq_rman) != 0 ||
	    rman_manage_region(&sc->oba_irq_rman, OCTEON_IRQ_UART0, OCTEON_IRQ_UART1) != 0)
		panic("obio_attach: failed to set up IRQ rman");

	device_add_child(dev, "uart", 1);  /* Setup Uart-1 first. */
	device_add_child(dev, "uart", 0);  /* Uart-0 next. So it is first in console list */
	bus_generic_probe(dev);
	bus_generic_attach(dev);
	return (0);
}

static struct resource *
obio_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource *rv;
	struct rman *rm;
	bus_space_tag_t bt = 0;
	bus_space_handle_t bh = 0;
	struct obio_softc *sc = device_get_softc(bus);

	switch (type) {
	case SYS_RES_IRQ:
		switch (device_get_unit(child)) {
		case 0:
			start = end = OCTEON_IRQ_UART0;
			break;
		case 1:
			start = end = OCTEON_IRQ_UART1;
			break;
		default:
			return (NULL);
		}
		rm = &sc->oba_irq_rman;
		break;
	case SYS_RES_MEMORY:
		return (NULL);
	case SYS_RES_IOPORT:
		rm = &sc->oba_rman;
		bt = &octeon_uart_tag;
		bh = CVMX_MIO_UARTX_RBR(device_get_unit(child));
		start = bh;
		break;
	default:
		return (NULL);
	}

	rv = rman_reserve_resource(rm, start, end, count, flags, child);
	if (rv == NULL)  {
		return (NULL);
        }
	if (type == SYS_RES_IRQ) {
		return (rv);
        }
	rman_set_rid(rv, *rid);
	rman_set_bustag(rv, bt);
	rman_set_bushandle(rv, bh);
	
	if (0) {
		if (bus_activate_resource(child, type, *rid, rv)) {
			rman_release_resource(rv);
			return (NULL);
		}
	}
	return (rv);

}

static int
obio_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
	return (0);
}
static device_method_t obio_methods[] = {
	/* Device methods */
	DEVMETHOD(device_identify,	obio_identify),
	DEVMETHOD(device_probe,		obio_probe),
	DEVMETHOD(device_attach,	obio_attach),

	/* Bus methods */
	DEVMETHOD(bus_alloc_resource,	obio_alloc_resource),
	DEVMETHOD(bus_activate_resource,obio_activate_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	DEVMETHOD(bus_add_child,	bus_generic_add_child),

	{0, 0},
};

static driver_t obio_driver = {
	"obio",
	obio_methods,
	sizeof(struct obio_softc),
};
static devclass_t obio_devclass;

DRIVER_MODULE(obio, ciu, obio_driver, obio_devclass, 0, 0);
