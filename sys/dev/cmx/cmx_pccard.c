/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2007 Daniel Roethlisberger <daniel@roe.ch>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/selinfo.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/cmx/cmxvar.h>

#include <dev/pccard/pccardvar.h>
#include <dev/pccard/pccard_cis.h>

#include "pccarddevs.h"

static const struct pccard_product cmx_pccard_products[] = {
	PCMCIA_CARD(OMNIKEY, CM4040),
	{ NULL }
};

/*
 * Probe for the card.
 */
static int
cmx_pccard_probe(device_t dev)
{
	const struct pccard_product *pp;
	if ((pp = pccard_product_lookup(dev, cmx_pccard_products,
	    sizeof(cmx_pccard_products[0]), NULL)) != NULL) {
		if (pp->pp_name != NULL)
			device_set_desc(dev, pp->pp_name);
		return 0;
	}
	return EIO;
}

/*
 * Attach to the pccard, and call bus independent attach and
 * resource allocation routines.
 */
static int
cmx_pccard_attach(device_t dev)
{
	int rv = 0;
	cmx_init_softc(dev);

	if ((rv = cmx_alloc_resources(dev)) != 0) {
		device_printf(dev, "cmx_alloc_resources() failed!\n");
		cmx_release_resources(dev);
		return rv;
	}

	if ((rv = cmx_attach(dev)) != 0) {
		device_printf(dev, "cmx_attach() failed!\n");
		cmx_release_resources(dev);
		return rv;
	}

	device_printf(dev, "attached\n");
	return 0;
}

static device_method_t cmx_pccard_methods[] = {
	DEVMETHOD(device_probe, cmx_pccard_probe),
	DEVMETHOD(device_attach, cmx_pccard_attach),
	DEVMETHOD(device_detach, cmx_detach),

	{ 0, 0 }
};

static driver_t cmx_pccard_driver = {
	"cmx",
	cmx_pccard_methods,
	sizeof(struct cmx_softc),
};

DRIVER_MODULE(cmx, pccard, cmx_pccard_driver, cmx_devclass, 0, 0);
PCCARD_PNP_INFO(cmx_pccard_products);
