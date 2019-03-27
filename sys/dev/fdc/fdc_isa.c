/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 M. Warner Losh.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <dev/fdc/fdcvar.h>

#include <isa/isavar.h>
#include <isa/isareg.h>

static int fdc_isa_probe(device_t);

static struct isa_pnp_id fdc_ids[] = {
	{0x0007d041, "PC standard floppy controller"}, /* PNP0700 */
	{0x0107d041, "Standard floppy controller supporting MS Device Bay Spec"}, /* PNP0701 */
	{0}
};

/*
 * On standard ISA, we don't just use an 8 port range
 * (e.g. 0x3f0-0x3f7) since that covers an IDE control register at
 * 0x3f6.  So, on older hardware, we use 0x3f0-0x3f5 and 0x3f7.
 * However, some BIOSs omit the control port, while others start at
 * 0x3f2.  Of the latter, sometimes we have two resources, other times
 * we have one.  We have to deal with the following cases:
 *
 * 1:	0x3f0-0x3f5			# very rare
 * 2:	0x3f0				# hints -> 0x3f0-0x3f5,0x3f7
 * 3:	0x3f0-0x3f5,0x3f7		# Most common
 * 4:	0x3f2-0x3f5,0x3f7		# Second most common
 * 5:	0x3f2-0x3f5			# implies 0x3f7 too.
 * 6:	0x3f2-0x3f3,0x3f4-0x3f5,0x3f7	# becoming common
 * 7:	0x3f2-0x3f3,0x3f4-0x3f5		# rare
 * 8:	0x3f0-0x3f1,0x3f2-0x3f3,0x3f4-0x3f5,0x3f7
 * 9:	0x3f0-0x3f3,0x3f4-0x3f5,0x3f7
 *
 * The following code is generic for any value of 0x3fx.  It is also
 * generic for all the above cases, as well as cases where things are
 * even weirder.
 */
int
fdc_isa_alloc_resources(device_t dev, struct fdc_data *fdc)
{
	struct resource *res;
	int i, j, rid, newrid, nport;
	u_long port;

	fdc->fdc_dev = dev;
	rid = 0;
	for (i = 0; i < FDC_MAXREG; i++)
		fdc->resio[i] = NULL;

	nport = isa_get_logicalid(dev) ? 1 : 6;
	for (rid = 0; ; rid++) {
		newrid = rid;
		res = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &newrid,
		    rid == 0 ? nport : 1, RF_ACTIVE);
		if (res == NULL)
			break;
		/*
		 * Mask off the upper bits of the register, and sanity
		 * check resource ranges.
		 */
		i = rman_get_start(res) & 0x7;
		if (i + rman_get_size(res) - 1 > FDC_MAXREG) {
			bus_release_resource(dev, SYS_RES_IOPORT, newrid, res);
			return (ENXIO);
		}
		for (j = 0; j < rman_get_size(res); j++) {
			fdc->resio[i + j] = res;
			fdc->ridio[i + j] = newrid;
			fdc->ioff[i + j] = j;
			fdc->ioh[i + j] = rman_get_bushandle(res);
		}
	}
	if (fdc->resio[2] == NULL) {
		device_printf(dev, "No FDOUT register!\n");
		return (ENXIO);
	}
	fdc->iot = rman_get_bustag(fdc->resio[2]);
	if (fdc->resio[7] == NULL) {
		port = (rman_get_start(fdc->resio[2]) & ~0x7) + 7;
		newrid = rid;
		res = bus_alloc_resource(dev, SYS_RES_IOPORT, &newrid, port,
		    port, 1, RF_ACTIVE);
		if (res == NULL) {
			device_printf(dev, "Faking up FDCTL\n");
			fdc->resio[7] = fdc->resio[2];
			fdc->ridio[7] = fdc->ridio[2];
			fdc->ioff[7] = fdc->ioff[2] + 5;
			fdc->ioh[7] = fdc->ioh[2];
		} else {
			fdc->resio[7] = res;
			fdc->ridio[7] = newrid;
			fdc->ioff[7] = rman_get_start(res) & 7;
			fdc->ioh[7] = rman_get_bushandle(res);
		}
	}

	fdc->res_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &fdc->rid_irq,
	    RF_ACTIVE | RF_SHAREABLE);
	if (fdc->res_irq == NULL) {
		device_printf(dev, "cannot reserve interrupt line\n");
		return (ENXIO);
	}

	if ((fdc->flags & FDC_NODMA) == 0) {
		fdc->res_drq = bus_alloc_resource_any(dev, SYS_RES_DRQ,
		    &fdc->rid_drq, RF_ACTIVE | RF_SHAREABLE);
		if (fdc->res_drq == NULL) {
			device_printf(dev, "cannot reserve DMA request line\n");
			/* This is broken and doesn't work for ISA case */
			fdc->flags |= FDC_NODMA;
		} else
			fdc->dmachan = rman_get_start(fdc->res_drq);
	}

	return (0);
}

static int
fdc_isa_probe(device_t dev)
{
	int	error;
	struct	fdc_data *fdc;

	fdc = device_get_softc(dev);
	fdc->fdc_dev = dev;

	/* Check pnp ids */
	error = ISA_PNP_PROBE(device_get_parent(dev), dev, fdc_ids);
	if (error == ENXIO)
		return (ENXIO);

	/* Attempt to allocate our resources for the duration of the probe */
	error = fdc_isa_alloc_resources(dev, fdc);
	if (error == 0)
		error = fdc_initial_reset(dev, fdc);

	fdc_release_resources(fdc);
	return (error);
}

static int
fdc_isa_attach(device_t dev)
{
	struct	fdc_data *fdc;
	int error;

	fdc = device_get_softc(dev);
	fdc->fdc_dev = dev;
	error = fdc_isa_alloc_resources(dev, fdc);
	if (error == 0)
		error = fdc_attach(dev);
	if (error == 0)
		error = fdc_hints_probe(dev);
	if (error == 0)
		fdc_start_worker(dev);
	else
		fdc_release_resources(fdc);
	return (error);
}

static device_method_t fdc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		fdc_isa_probe),
	DEVMETHOD(device_attach,	fdc_isa_attach),
	DEVMETHOD(device_detach,	fdc_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	fdc_print_child),
	DEVMETHOD(bus_read_ivar,	fdc_read_ivar),
	DEVMETHOD(bus_write_ivar,       fdc_write_ivar),
	/* Our children never use any other bus interface methods. */

	{ 0, 0 }
};

static driver_t fdc_driver = {
	"fdc",
	fdc_methods,
	sizeof(struct fdc_data)
};

DRIVER_MODULE(fdc, isa, fdc_driver, fdc_devclass, 0, 0);
ISA_PNP_INFO(fdc_ids);
