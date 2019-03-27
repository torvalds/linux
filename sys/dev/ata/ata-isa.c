/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 - 2008 Søren Schmidt <sos@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
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
#include <sys/ata.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <vm/uma.h>
#include <machine/stdarg.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <isa/isavar.h>
#include <dev/ata/ata-all.h>
#include <ata_if.h>

/* local vars */
static struct isa_pnp_id ata_ids[] = {
    {0x0006d041,        "Generic ESDI/IDE/ATA controller"},     /* PNP0600 */
    {0x0106d041,        "Plus Hardcard II"},                    /* PNP0601 */
    {0x0206d041,        "Plus Hardcard IIXL/EZ"},               /* PNP0602 */
    {0x0306d041,        "Generic ATA"},                         /* PNP0603 */
								/* PNP0680 */
    {0x8006d041,        "Standard bus mastering IDE hard disk controller"},
    {0}
};

static int
ata_isa_probe(device_t dev)
{
    struct resource *io = NULL, *ctlio = NULL;
    rman_res_t tmp;
    int rid;

    /* check isapnp ids */
    if (ISA_PNP_PROBE(device_get_parent(dev), dev, ata_ids) == ENXIO)
	return ENXIO;

    /* allocate the io port range */
    rid = ATA_IOADDR_RID;
    if (!(io = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &rid,
					   ATA_IOSIZE, RF_ACTIVE)))
	return ENXIO;

    /* set the altport range */
    if (bus_get_resource(dev, SYS_RES_IOPORT, ATA_CTLADDR_RID, &tmp, &tmp)) {
	bus_set_resource(dev, SYS_RES_IOPORT, ATA_CTLADDR_RID,
			 rman_get_start(io) + ATA_CTLOFFSET, ATA_CTLIOSIZE);
    }

    /* allocate the altport range */
    rid = ATA_CTLADDR_RID; 
    if (!(ctlio = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &rid,
					      ATA_CTLIOSIZE, RF_ACTIVE))) {
	bus_release_resource(dev, SYS_RES_IOPORT, ATA_IOADDR_RID, io);
	return ENXIO;
    }

    /* Release resources to reallocate on attach. */
    bus_release_resource(dev, SYS_RES_IOPORT, ATA_CTLADDR_RID, ctlio);
    bus_release_resource(dev, SYS_RES_IOPORT, ATA_IOADDR_RID, io);

    device_set_desc(dev, "ATA channel");
    return (ata_probe(dev));
}

static int
ata_isa_attach(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    struct resource *io = NULL, *ctlio = NULL;
    rman_res_t tmp;
    int i, rid;

    if (ch->attached)
	return (0);
    ch->attached = 1;

    /* allocate the io port range */
    rid = ATA_IOADDR_RID;
    if (!(io = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &rid,
					   ATA_IOSIZE, RF_ACTIVE)))
	return ENXIO;

    /* set the altport range */
    if (bus_get_resource(dev, SYS_RES_IOPORT, ATA_CTLADDR_RID, &tmp, &tmp)) {
	bus_set_resource(dev, SYS_RES_IOPORT, ATA_CTLADDR_RID,
			 rman_get_start(io) + ATA_CTLOFFSET, ATA_CTLIOSIZE);
    }

    /* allocate the altport range */
    rid = ATA_CTLADDR_RID; 
    if (!(ctlio = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &rid,
					      ATA_CTLIOSIZE, RF_ACTIVE))) {
	bus_release_resource(dev, SYS_RES_IOPORT, ATA_IOADDR_RID, io);
	return ENXIO;
    }

    /* setup the resource vectors */
    for (i = ATA_DATA; i <= ATA_COMMAND; i++) {
	ch->r_io[i].res = io;
	ch->r_io[i].offset = i;
    }
    ch->r_io[ATA_CONTROL].res = ctlio;
    ch->r_io[ATA_CONTROL].offset = 0;
    ch->r_io[ATA_IDX_ADDR].res = io;
    ata_default_registers(dev);
 
    /* initialize softc for this channel */
    ch->unit = 0;
    ch->flags |= ATA_USE_16BIT;
    ata_generic_hw(dev);
    return ata_attach(dev);
}

static int
ata_isa_detach(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);
    int error;

    if (!ch->attached)
	return (0);
    ch->attached = 0;

    error = ata_detach(dev);

    bus_release_resource(dev, SYS_RES_IOPORT, ATA_CTLADDR_RID,
	ch->r_io[ATA_CONTROL].res);
    bus_release_resource(dev, SYS_RES_IOPORT, ATA_IOADDR_RID,
	ch->r_io[ATA_IDX_ADDR].res);
    return (error);
}

static int
ata_isa_suspend(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    if (!ch->attached)
	return (0);

    return ata_suspend(dev);
}

static int
ata_isa_resume(device_t dev)
{
    struct ata_channel *ch = device_get_softc(dev);

    if (!ch->attached)
	return (0);

    return ata_resume(dev);
}


static device_method_t ata_isa_methods[] = {
    /* device interface */
    DEVMETHOD(device_probe,     ata_isa_probe),
    DEVMETHOD(device_attach,    ata_isa_attach),
    DEVMETHOD(device_detach,    ata_isa_detach),
    DEVMETHOD(device_suspend,   ata_isa_suspend),
    DEVMETHOD(device_resume,    ata_isa_resume),

    DEVMETHOD_END
};

static driver_t ata_isa_driver = {
    "ata",
    ata_isa_methods,
    sizeof(struct ata_channel),
};

DRIVER_MODULE(ata, isa, ata_isa_driver, ata_devclass, NULL, NULL);
MODULE_DEPEND(ata, ata, 1, 1, 1);
ISA_PNP_INFO(ata_ids);
