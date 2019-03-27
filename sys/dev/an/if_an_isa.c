/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Aironet 4500/4800 802.11 PCMCIA/ISA/PCI driver for FreeBSD.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"

#ifdef INET
#define ANCACHE
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>

#include <sys/module.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_media.h>

#include <isa/isavar.h>
#include <isa/pnpvar.h>

#include <dev/an/if_aironet_ieee.h>
#include <dev/an/if_anreg.h>

static struct isa_pnp_id an_ids[] = {
	{ 0x0100ec06, "Aironet ISA4500/ISA4800" },
	{ 0, NULL }
};

static int an_probe_isa(device_t);
static int an_attach_isa(device_t);

static int
an_probe_isa(device_t dev)
{
	int			error = 0;

	error = ISA_PNP_PROBE(device_get_parent(dev), dev, an_ids);
	if (error == ENXIO)
		return(error);

	error = an_probe(dev);
	an_release_resources(dev);
	if (error == 0)
		return (ENXIO);

	error = an_alloc_irq(dev, 0, 0);
	an_release_resources(dev);
	if (!error)
		device_set_desc(dev, "Aironet ISA4500/ISA4800");
	return (error);
}

static int
an_attach_isa(device_t dev)
{
	struct an_softc *sc = device_get_softc(dev);
	int flags = device_get_flags(dev);
	int error;

	an_alloc_port(dev, sc->port_rid, 1);
	an_alloc_irq(dev, sc->irq_rid, 0);

	sc->an_dev = dev;

	error = an_attach(sc, flags);
	if (error) {
		an_release_resources(dev);
		return (error);
	}

	error = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_NET,
			       NULL, an_intr, sc, &sc->irq_handle);
	if (error) {
		an_release_resources(dev);
		return (error);
	}
	return (0);
}

static device_method_t an_isa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		an_probe_isa),
	DEVMETHOD(device_attach,	an_attach_isa),
	DEVMETHOD(device_detach,	an_detach),
	DEVMETHOD(device_shutdown,	an_shutdown),
	{ 0, 0 }
};

static driver_t an_isa_driver = {
	"an",
	an_isa_methods,
	sizeof(struct an_softc)
};

static devclass_t an_isa_devclass;

DRIVER_MODULE(an, isa, an_isa_driver, an_isa_devclass, 0, 0);
MODULE_DEPEND(an, isa, 1, 1, 1);
MODULE_DEPEND(an, wlan, 1, 1, 1);
ISA_PNP_INFO(an_ids);
