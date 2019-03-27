/*-
 * Copyright (c) 1997-2001 Granch, Ltd. All rights reserved.
 * Author: Denis I.Timofeev <timofeev@granch.ru>
 *
 * Redistributon and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * LIABILITY, OR TORT (INCLUDING NEIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>

#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>

#include <isa/isavar.h>

#include <dev/sbni/if_sbnireg.h>
#include <dev/sbni/if_sbnivar.h>

static int	sbni_probe_isa(device_t);
static int	sbni_attach_isa(device_t);

static device_method_t sbni_isa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,	sbni_probe_isa),
	DEVMETHOD(device_attach, sbni_attach_isa),
	{ 0, 0 }
};

static driver_t sbni_isa_driver = {
	"sbni",
	sbni_isa_methods,
	sizeof(struct sbni_softc)
};

static devclass_t sbni_isa_devclass;
static struct isa_pnp_id  sbni_ids[] = {
	{ 0, NULL }	/* we have no pnp sbni cards atm.  */
};

static int
sbni_probe_isa(device_t dev)
{
	struct sbni_softc *sc;
	int error;

	error = ISA_PNP_PROBE(device_get_parent(dev), dev, sbni_ids);
	if (error && error != ENOENT)
		return (error);

	sc = device_get_softc(dev);

 	sc->io_res = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT,
 						 &sc->io_rid, SBNI_PORTS,
 						 RF_ACTIVE);
	if (!sc->io_res) {
		printf("sbni: cannot allocate io ports!\n");
		return (ENOENT);
	}

	if (sbni_probe(sc) != 0) {
		sbni_release_resources(sc);
		return (ENXIO);
	}

	device_set_desc(dev, "Granch SBNI12/ISA adapter");
	return (0);
}


static int
sbni_attach_isa(device_t dev)
{
	struct sbni_softc *sc;
	struct sbni_flags flags;
	int error;
   
	sc = device_get_softc(dev);
	sc->dev = dev;

	sc->irq_res = bus_alloc_resource_any(
	    dev, SYS_RES_IRQ, &sc->irq_rid, RF_ACTIVE);

#ifndef SBNI_DUAL_COMPOUND

	if (sc->irq_res == NULL) {
		device_printf(dev, "irq conflict!\n");
		sbni_release_resources(sc);
		return (ENOENT);
	}

#else	/* SBNI_DUAL_COMPOUND */

	if (sc->irq_res) {
		sbni_add(sc);
	} else {
		struct sbni_softc  *master;

		if ((master = connect_to_master(sc)) == NULL) {
			device_printf(dev, "failed to alloc irq\n");
			sbni_release_resources(sc);
			return (ENXIO);
		} else {
			device_printf(dev, "shared irq with %s\n",
			       master->ifp->if_xname);
		}
	} 
#endif	/* SBNI_DUAL_COMPOUND */

	*(u_int32_t*)&flags = device_get_flags(dev);

	error = sbni_attach(sc, device_get_unit(dev) * 2, flags);
	if (error) {
		device_printf(dev, "cannot initialize driver\n");
		sbni_release_resources(sc);
		return (error);
	}

	if (sc->irq_res) {
		error = bus_setup_intr(
		    dev, sc->irq_res, INTR_TYPE_NET | INTR_MPSAFE,
		    NULL, sbni_intr, sc, &sc->irq_handle);
		if (error) {
			device_printf(dev, "bus_setup_intr\n");
			sbni_detach(sc);
			sbni_release_resources(sc);
			return (error);
		}
	}

	return (0);
}

DRIVER_MODULE(sbni, isa, sbni_isa_driver, sbni_isa_devclass, 0, 0);
MODULE_DEPEND(sbni, isa, 1, 1, 1);
ISA_PNP_INFO(sbni_ids);
