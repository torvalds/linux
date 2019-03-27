/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 M. Warner Losh <imp@village.org> 
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <sys/socket.h>

#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
 
#include <net/ethernet.h> 
#include <net/if.h> 
#include <net/if_media.h>

#include <dev/cs/if_csvar.h>
#include <dev/cs/if_csreg.h>
#include <dev/pccard/pccardvar.h>
#include <dev/pccard/pccard_cis.h>

#include "card_if.h"
#include "pccarddevs.h"

static const struct pccard_product cs_pccard_products[] = {
	PCMCIA_CARD(IBM, ETHERJET),
	{ NULL }
};

static int
cs_pccard_probe(device_t dev)
{
	const struct pccard_product *pp;
	uint32_t	fcn = PCCARD_FUNCTION_UNSPEC;

	/* Make sure we're a network function */
	pccard_get_function(dev, &fcn);
	if (fcn != PCCARD_FUNCTION_NETWORK)
		return (ENXIO);

	if ((pp = pccard_product_lookup(dev, cs_pccard_products,
	    sizeof(cs_pccard_products[0]), NULL)) != NULL) {
		if (pp->pp_name != NULL)
			device_set_desc(dev, pp->pp_name);
		return 0;
	}
	return EIO;
}

static int
cs_pccard_attach(device_t dev)
{
	struct cs_softc *sc = device_get_softc(dev);
	int error;

	sc->flags |= CS_NO_IRQ;
	error = cs_cs89x0_probe(dev);
	if (error != 0)
		return (error);
	error = cs_alloc_irq(dev, sc->irq_rid);
	if (error != 0)
		goto bad;

	return (cs_attach(dev));
bad:
	cs_release_resources(dev);
	return (error);
}

static device_method_t cs_pccard_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cs_pccard_probe),
	DEVMETHOD(device_attach,	cs_pccard_attach),
	DEVMETHOD(device_detach,	cs_detach),

	{ 0, 0 }
};

static driver_t cs_pccard_driver = {
	"cs",
	cs_pccard_methods,
	sizeof(struct cs_softc),
};

extern devclass_t cs_devclass;

DRIVER_MODULE(cs, pccard, cs_pccard_driver, cs_devclass, 0, 0);
MODULE_DEPEND(cs, ether, 1, 1, 1);
PCCARD_PNP_INFO(cs_pccard_products);
