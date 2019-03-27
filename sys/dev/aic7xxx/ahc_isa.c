/*-
 * FreeBSD, VLB/ISA product support functions
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2004 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/aic7xxx/aic7xxx_osm.h>

#include <sys/limits.h>		/* For CHAR_BIT*/
#include <isa/isavar.h>		/* For ISA attach glue */


static struct aic7770_identity *ahc_isa_find_device(bus_space_tag_t tag,
						    bus_space_handle_t bsh);
static void			ahc_isa_identify(driver_t *driver,
						 device_t parent);
static int			ahc_isa_probe(device_t dev);
static int			ahc_isa_attach(device_t dev);

/*
 * Perform an EISA probe of the address with the addition
 * of a "priming" step.  The 284X requires priming (a write
 * to offset 0x80, the first EISA ID register) to ensure it
 * is not mistaken as an EISA card.  Once we have the ID,
 * lookup the controller in the aic7770 table of supported
 * devices.
 */
static struct aic7770_identity *
ahc_isa_find_device(bus_space_tag_t tag, bus_space_handle_t bsh) {
	uint32_t  id;
	u_int	  id_size;
	int	  i;

	id = 0;
	id_size = sizeof(id);
	for (i = 0; i < id_size; i++) {
		bus_space_write_1(tag, bsh, 0x80, 0x80 + i);
		id |= bus_space_read_1(tag, bsh, 0x80 + i)
		   << ((id_size - i - 1) * CHAR_BIT);
	}
                           
	return (aic7770_find_device(id));
}

static void
ahc_isa_identify(driver_t *driver, device_t parent)
{
	int slot;
	int max_slot;

	max_slot = 14;
	for (slot = 0; slot <= max_slot; slot++) {
		struct aic7770_identity *entry;
		bus_space_tag_t	    tag;
		bus_space_handle_t  bsh;
		struct resource	   *regs;
		uint32_t	    iobase;
		int		    rid;

		rid = 0;
		iobase = (slot * AHC_EISA_SLOT_SIZE) + AHC_EISA_SLOT_OFFSET;
		regs = bus_alloc_resource(parent, SYS_RES_IOPORT, &rid,
					  iobase, iobase, AHC_EISA_IOSIZE,
					  RF_ACTIVE);
		if (regs == NULL) {
			if (bootverbose)
				printf("ahc_isa_identify %d: ioport 0x%x "
				       "alloc failed\n", slot, iobase);
			continue;
		}

		tag = rman_get_bustag(regs);
		bsh = rman_get_bushandle(regs);

		entry = ahc_isa_find_device(tag, bsh);
		if (entry != NULL) {
			device_t child;

			child = BUS_ADD_CHILD(parent, ISA_ORDER_SPECULATIVE,
					      "ahc", -1);
			if (child != NULL) {
				device_set_driver(child, driver);
				bus_set_resource(child, SYS_RES_IOPORT,
						 0, iobase, AHC_EISA_IOSIZE);
			}
		}
		bus_release_resource(parent, SYS_RES_IOPORT, rid, regs);
	}
}

static int
ahc_isa_probe(device_t dev)
{
	struct	  aic7770_identity *entry;
	bus_space_tag_t	    tag;
	bus_space_handle_t  bsh;
	struct	  resource *regs;
	struct	  resource *irq;
	uint32_t  iobase;
	u_int	  intdef;
	u_int	  hcntrl;
	int	  irq_num;
	int	  error;
	int	  zero;

	error = ENXIO;
	zero = 0;
	regs = NULL;
	irq = NULL;

	/* Skip probes for ISA PnP devices */
	if (isa_get_logicalid(dev) != 0)
		return (error);

	regs = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &zero, RF_ACTIVE);
	if (regs == NULL) {
		device_printf(dev, "No resources allocated.\n");
		return (ENOMEM);
	}

	iobase = rman_get_start(regs);
	tag = rman_get_bustag(regs);
	bsh = rman_get_bushandle(regs);

	entry = ahc_isa_find_device(tag, bsh);
	if (entry == NULL)
		goto cleanup;

	/* Pause the card preseving the IRQ type */
	hcntrl = bus_space_read_1(tag, bsh, HCNTRL) & IRQMS;
	bus_space_write_1(tag, bsh, HCNTRL, hcntrl | PAUSE);
	while ((bus_space_read_1(tag, bsh, HCNTRL) & PAUSE) == 0)
		;

	/* Make sure we have a valid interrupt vector */
	intdef = bus_space_read_1(tag, bsh, INTDEF);
	irq_num = intdef & VECTOR;
	switch (irq_num) {
	case 9: 
	case 10:
	case 11:
	case 12:
	case 14:
	case 15:
		break;
	default:
		device_printf(dev, "@0x%x: illegal irq setting %d\n",
			      iobase, irq_num);
		goto cleanup;
	}

	if (bus_set_resource(dev, SYS_RES_IRQ, zero, irq_num, 1) != 0)
		goto cleanup;

	/*
	 * The 284X only supports edge triggered interrupts,
	 * so do not claim RF_SHAREABLE.
	 */
	irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &zero,
				     0 /*!(RF_ACTIVE|RF_SHAREABLE)*/);
	if (irq != NULL) {
		error = 0;
		device_set_desc(dev, entry->name);
	} else 
		device_printf(dev, "@0x%x: irq %d allocation failed\n",
			      iobase, irq_num);

cleanup:
	if (regs != NULL) {
		bus_release_resource(dev, SYS_RES_IOPORT, zero, regs);
		regs = NULL;
	}

	if (irq != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, zero, irq);
		irq = NULL;
	}

	return (error);
}

static int
ahc_isa_attach(device_t dev)
{
	struct	 aic7770_identity *entry;
	bus_space_tag_t	    tag;
	bus_space_handle_t  bsh;
	struct	  resource *regs;
	struct	  ahc_softc *ahc;
	char	 *name;
	int	  zero;
	int	  error;

	zero = 0;
	regs = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &zero, RF_ACTIVE);
	if (regs == NULL)
		return (ENOMEM);

	tag = rman_get_bustag(regs);
	bsh = rman_get_bushandle(regs);
	entry = ahc_isa_find_device(tag, bsh);
	bus_release_resource(dev, SYS_RES_IOPORT, zero, regs);
	if (entry == NULL)
		return (ENODEV);

	/*
	 * Allocate a softc for this card and
	 * set it up for attachment by our
	 * common detect routine.
	 */
	name = malloc(strlen(device_get_nameunit(dev)) + 1, M_DEVBUF, M_NOWAIT);
	if (name == NULL)
		return (ENOMEM);
	strcpy(name, device_get_nameunit(dev));
	ahc = ahc_alloc(dev, name);
	if (ahc == NULL)
		return (ENOMEM);

	ahc_set_unit(ahc, device_get_unit(dev));

	/* Allocate a dmatag for our SCB DMA maps */
	error = aic_dma_tag_create(ahc, /*parent*/bus_get_dma_tag(dev),
				   /*alignment*/1, /*boundary*/0,
				   /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
				   /*highaddr*/BUS_SPACE_MAXADDR,
				   /*filter*/NULL, /*filterarg*/NULL,
				   /*maxsize*/BUS_SPACE_MAXSIZE_32BIT,
				   /*nsegments*/AHC_NSEG,
				   /*maxsegsz*/AHC_MAXTRANSFER_SIZE,
				   /*flags*/0,
				   &ahc->parent_dmat);

	if (error != 0) {
		printf("ahc_isa_attach: Could not allocate DMA tag "
		       "- error %d\n", error);
		ahc_free(ahc);
		return (ENOMEM);
	}
	ahc->dev_softc = dev;
	error = aic7770_config(ahc, entry, /*unused ioport arg*/0);
	if (error != 0) {
		ahc_free(ahc);
		return (error);
	}

	ahc_attach(ahc);
	return (0);
}

static device_method_t ahc_isa_device_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,      ahc_isa_identify),
	DEVMETHOD(device_probe,		ahc_isa_probe),
	DEVMETHOD(device_attach,	ahc_isa_attach),
	DEVMETHOD(device_detach,	ahc_detach),
	{ 0, 0 }
};

static driver_t ahc_isa_driver = {
	"ahc",
	ahc_isa_device_methods,
	sizeof(struct ahc_softc)
};

DRIVER_MODULE(ahc_isa, isa, ahc_isa_driver, ahc_devclass, 0, 0);
MODULE_DEPEND(ahc_isa, ahc, 1, 1, 1);
MODULE_VERSION(ahc_isa, 1);
