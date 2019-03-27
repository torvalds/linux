/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2002 by Peter Grehan. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * PSIM local bus ATA controller
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <machine/stdarg.h>
#include <vm/uma.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/ata.h>
#include <dev/ata/ata-all.h>
#include <ata_if.h>

#include <dev/ofw/openfirm.h>
#include <powerpc/psim/iobusvar.h>

/*
 * Define the iobus ata bus attachment. This creates a pseudo-bus that
 * the ATA device can be attached to
 */
static  int  ata_iobus_attach(device_t dev);
static  int  ata_iobus_probe(device_t dev);
static  int  ata_iobus_print_child(device_t dev, device_t child);
struct resource *ata_iobus_alloc_resource(device_t, device_t, int, int *,
					  rman_res_t, rman_res_t, rman_res_t,
					  u_int);
static int ata_iobus_release_resource(device_t, device_t, int, int,
				      struct resource *);

static device_method_t ata_iobus_methods[] = {
        /* Device interface */
	DEVMETHOD(device_probe,		ata_iobus_probe),
	DEVMETHOD(device_attach,        ata_iobus_attach),
	DEVMETHOD(device_shutdown,      bus_generic_shutdown),
	DEVMETHOD(device_suspend,       bus_generic_suspend),
	DEVMETHOD(device_resume,        bus_generic_resume),

	/* Bus methods */
	DEVMETHOD(bus_print_child,          ata_iobus_print_child),
	DEVMETHOD(bus_alloc_resource,       ata_iobus_alloc_resource),
	DEVMETHOD(bus_release_resource,     ata_iobus_release_resource),
	DEVMETHOD(bus_activate_resource,    bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource,  bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,           bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,        bus_generic_teardown_intr),

	DEVMETHOD_END
};

static driver_t ata_iobus_driver = {
	"ataiobus",
	ata_iobus_methods,
	0,
};

static devclass_t ata_iobus_devclass;

DRIVER_MODULE(ataiobus, iobus, ata_iobus_driver, ata_iobus_devclass, NULL,
    NULL);
MODULE_DEPEND(ata, ata, 1, 1, 1);

static int
ata_iobus_probe(device_t dev)
{
	char *type = iobus_get_name(dev);

	if (strncmp(type, "ata", 3) != 0)
		return (ENXIO);

	device_set_desc(dev, "PSIM ATA Controller");
	return (0);
}


static int
ata_iobus_attach(device_t dev)
{
	/*
	 * Add a single child per controller. Should be able
	 * to add two
	 */
	device_add_child(dev, "ata", -1);
	return (bus_generic_attach(dev));
}


static int
ata_iobus_print_child(device_t dev, device_t child)
{
	int retval = 0;

	retval += bus_print_child_header(dev, child);
	/* irq ? */
	retval += bus_print_child_footer(dev, child);

	return (retval);
}


struct resource *
ata_iobus_alloc_resource(device_t dev, device_t child, int type, int *rid,
			 rman_res_t start, rman_res_t end, rman_res_t count,
			 u_int flags)
{
	struct resource *res = NULL;
	int myrid;
	u_int *ofw_regs;

	ofw_regs = iobus_get_regs(dev);

	/*
	 * The reg array for the PSIM ata device has 6 start/size entries:
	 *  0 - unused
	 *  1/2/3 - unused
	 *  4/5/6 - primary command
	 *  7/8/9 - secondary command
	 *  10/11/12 - primary control
	 *  13/14/15 - secondary control
	 *  16/17/18 - primary/secondary dma registers, unimplemented
	 *
	 *  The resource values are calculated from these registers
	 */
	if (type == SYS_RES_IOPORT) {
		switch (*rid) {
		case ATA_IOADDR_RID:
			myrid = 0;
			start = ofw_regs[4];
			end = start + ATA_IOSIZE - 1;
			count = ATA_IOSIZE;
			res = BUS_ALLOC_RESOURCE(device_get_parent(dev), child,
						 SYS_RES_MEMORY, &myrid,
						 start, end, count, flags);
			break;

		case ATA_CTLADDR_RID:
			myrid = 0;
			start = ofw_regs[10];
			end = start + ATA_CTLIOSIZE - 1;
			count = ATA_CTLIOSIZE;
			res = BUS_ALLOC_RESOURCE(device_get_parent(dev), child,
						 SYS_RES_MEMORY, &myrid,
						 start, end, count, flags);
			break;

		case ATA_BMADDR_RID:
			/* DMA not properly supported by psim */
			break;
		}
		return (res);

	} else if (type == SYS_RES_IRQ && *rid == ATA_IRQ_RID) {
		/*
		 * Pass this on to the parent
		 */
		return (BUS_ALLOC_RESOURCE(device_get_parent(dev), dev,
					   SYS_RES_IRQ, rid, 0, ~0, 1, flags));

	} else {
		return (NULL);
	}
}


static int
ata_iobus_release_resource(device_t dev, device_t child, int type, int rid,
			   struct resource *r)
{
	/* no hotplug... */
	return (0);
}


/*
 * Define the actual ATA device. This is a sub-bus to the ata-iobus layer
 * to allow the higher layer bus to massage the resource allocation.
 */

static  int  ata_iobus_sub_probe(device_t dev);
static  int  ata_iobus_sub_setmode(device_t dev, int target, int mode);

static device_method_t ata_iobus_sub_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,     ata_iobus_sub_probe),
	DEVMETHOD(device_attach,    ata_attach),
	DEVMETHOD(device_detach,    ata_detach),
	DEVMETHOD(device_resume,    ata_resume),

	/* ATA interface */
	DEVMETHOD(ata_setmode,	    ata_iobus_sub_setmode),
	DEVMETHOD_END
};

static driver_t ata_iobus_sub_driver = {
	"ata",
	ata_iobus_sub_methods,
	sizeof(struct ata_channel),
};

DRIVER_MODULE(ata, ataiobus, ata_iobus_sub_driver, ata_devclass, NULL, NULL);

static int
ata_iobus_sub_probe(device_t dev)
{
	struct ata_channel *ch = device_get_softc(dev);

	/* Only a single unit per controller thus far */
	ch->unit = 0;
	ch->flags = (ATA_USE_16BIT|ATA_NO_SLAVE);
	ata_generic_hw(dev);

	return ata_probe(dev);
}

static int
ata_iobus_sub_setmode(device_t parent, int target, int mode)
{
	/* Only ever PIO mode here... */
	return (ATA_PIO);
}
