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
 * On-board device autoconfiguration support for Intel IQ80321
 * evaluation boards.
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

#include <mips/malta/maltareg.h>
#include <mips/malta/obiovar.h>

int	obio_probe(device_t);
int	obio_attach(device_t);

/*
 * A bit tricky and hackish. Since we need OBIO to rely
 * on PCI we make it pseudo-pci device. But there should 
 * be only one such device, so we use this static flag 
 * to prevent false positives on every real PCI device probe.
 */
static int have_one = 0;

int
obio_probe(device_t dev)
{
	if (!have_one) {
		have_one = 1;
		return 0;
	}
	return (ENXIO);
}

int
obio_attach(device_t dev)
{
	struct obio_softc *sc = device_get_softc(dev);

	sc->oba_st = mips_bus_space_generic;
	sc->oba_addr = MIPS_PHYS_TO_KSEG1(MALTA_UART0ADR);
	sc->oba_size = MALTA_PCIMEM3_SIZE;
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
	 * it's IRQ is 4
	 */
	if (rman_init(&sc->oba_irq_rman) != 0 ||
	    rman_manage_region(&sc->oba_irq_rman, 4, 4) != 0)
		panic("obio_attach: failed to set up IRQ rman");

	device_add_child(dev, "uart", 0);
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
		rm = &sc->oba_irq_rman;
		break;
	case SYS_RES_MEMORY:
		return (NULL);
	case SYS_RES_IOPORT:
		rm = &sc->oba_rman;
		bt = sc->oba_st;
		bh = sc->oba_addr;
		start = bh;
		break;
	default:
		return (NULL);
	}


	rv = rman_reserve_resource(rm, start, end, count, flags, child);
	if (rv == NULL) 
		return (NULL);
	if (type == SYS_RES_IRQ)
		return (rv);
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
	DEVMETHOD(device_probe, obio_probe),
	DEVMETHOD(device_attach, obio_attach),

	DEVMETHOD(bus_alloc_resource, obio_alloc_resource),
	DEVMETHOD(bus_activate_resource, obio_activate_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	{0, 0},
};

static driver_t obio_driver = {
	"obio",
	obio_methods,
	sizeof(struct obio_softc),
};
static devclass_t obio_devclass;

DRIVER_MODULE(obio, pci, obio_driver, obio_devclass, 0, 0);
