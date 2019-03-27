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
#include <sys/socket.h>
#include <sys/kernel.h>

#include <sys/module.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/resource.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_media.h>

#include <dev/an/if_aironet_ieee.h>
#include <dev/an/if_anreg.h>

#include <dev/pccard/pccardvar.h>

#include "pccarddevs.h"
#include "card_if.h"

/*
 * Support for PCMCIA cards.
 */
static int  an_pccard_probe(device_t);
static int  an_pccard_attach(device_t);

static device_method_t an_pccard_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		an_pccard_probe),
	DEVMETHOD(device_attach,	an_pccard_attach),
	DEVMETHOD(device_detach,	an_detach),
	DEVMETHOD(device_shutdown,	an_shutdown),

	{ 0, 0 }
};

static driver_t an_pccard_driver = {
	"an",
	an_pccard_methods,
	sizeof(struct an_softc)
};

static devclass_t an_pccard_devclass;

DRIVER_MODULE(an, pccard, an_pccard_driver, an_pccard_devclass, 0, 0);
MODULE_DEPEND(an, wlan, 1, 1, 1);

static const struct pccard_product an_pccard_products[] = {
	PCMCIA_CARD(AIRONET, PC4800),
	PCMCIA_CARD(AIRONET, PC4500),
	PCMCIA_CARD(AIRONET, 350),
	PCMCIA_CARD(XIRCOM, CWE1130), 
	{ NULL }
};
PCCARD_PNP_INFO(an_pccard_products);

static int
an_pccard_probe(device_t dev)
{
	const struct pccard_product *pp;

	if ((pp = pccard_product_lookup(dev, an_pccard_products,
	    sizeof(an_pccard_products[0]), NULL)) != NULL) {
		if (pp->pp_name != NULL)
			device_set_desc(dev, pp->pp_name);
		return (0);
	}
	return (ENXIO);
}

static int
an_pccard_attach(device_t dev)
{
	struct an_softc *sc = device_get_softc(dev);
	int flags = device_get_flags(dev);
	int     error;

	error = an_probe(dev); /* 0 is failure for now */
	if (error == 0) {
		error = ENXIO;
		goto fail;
	}
	error = an_alloc_irq(dev, 0, 0);
	if (error != 0)
		goto fail;

	an_alloc_irq(dev, sc->irq_rid, 0);

	error = an_attach(sc, flags);
	if (error)
		goto fail;
	
	/*
	 * Must setup the interrupt after the an_attach to prevent racing.
	 */
	error = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_NET,
			       NULL, an_intr, sc, &sc->irq_handle);
fail:
	if (error)
		an_release_resources(dev);
	return (error);
}
