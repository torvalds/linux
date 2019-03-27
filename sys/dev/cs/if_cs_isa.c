/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997,1998 Maxim Bolotin and Oleg Sharoiko.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
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

#include <isa/isavar.h>

#include <dev/cs/if_csvar.h>
#include <dev/cs/if_csreg.h>

static int		cs_isa_probe(device_t);
static int		cs_isa_attach(device_t);

static struct isa_pnp_id cs_ids[] = {
	{ 0x4060630e, NULL },		/* CSC6040 */
	{ 0x10104d24, NULL },		/* IBM EtherJet */
	{ 0, NULL }
};

/*
 * Determine if the device is present
 */
static int
cs_isa_probe(device_t dev)
{
	int error;

	/* Check isapnp ids */
	error = ISA_PNP_PROBE(device_get_parent(dev), dev, cs_ids);

	/* If the card had a PnP ID that didn't match any we know about */
	if (error == ENXIO)
                goto end;

        /* If we've matched, or there's no PNP ID, probe chip */
	if (error == 0 || error == ENOENT)
		error = cs_cs89x0_probe(dev);
end:
	/* Make sure IRQ is assigned for probe message and available */
	if (error == 0)
                error = cs_alloc_irq(dev, 0);

        cs_release_resources(dev);
        return (error);
}

static int
cs_isa_attach(device_t dev)
{
        struct cs_softc *sc = device_get_softc(dev);
        
	cs_alloc_port(dev, 0, CS_89x0_IO_PORTS);
        cs_alloc_irq(dev, sc->irq_rid);
                
        return (cs_attach(dev));
}

static device_method_t cs_isa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cs_isa_probe),
	DEVMETHOD(device_attach,	cs_isa_attach),
	DEVMETHOD(device_detach,	cs_detach),

	{ 0, 0 }
};

static driver_t cs_isa_driver = {
	"cs",
	cs_isa_methods,
	sizeof(struct cs_softc),
};

extern devclass_t cs_devclass;

DRIVER_MODULE(cs, isa, cs_isa_driver, cs_devclass, 0, 0);
MODULE_DEPEND(cs, isa, 1, 1, 1);
MODULE_DEPEND(cs, ether, 1, 1, 1);
ISA_PNP_INFO(cs_ids);
