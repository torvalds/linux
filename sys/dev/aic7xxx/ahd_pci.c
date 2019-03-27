/*-
 * FreeBSD, PCI product support functions
 *
 * Copyright (c) 1995-2001 Justin T. Gibbs
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: //depot/aic7xxx/freebsd/dev/aic7xxx/ahd_pci.c#17 $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/aic7xxx/aic79xx_osm.h>

static int ahd_pci_probe(device_t dev);
static int ahd_pci_attach(device_t dev);

static device_method_t ahd_pci_device_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ahd_pci_probe),
	DEVMETHOD(device_attach,	ahd_pci_attach),
	DEVMETHOD(device_detach,	ahd_detach),
	{ 0, 0 }
};

static driver_t ahd_pci_driver = {
	"ahd",
	ahd_pci_device_methods,
	sizeof(struct ahd_softc)
};

static devclass_t ahd_devclass;

DRIVER_MODULE(ahd, pci, ahd_pci_driver, ahd_devclass, 0, 0);
MODULE_DEPEND(ahd_pci, ahd, 1, 1, 1);
MODULE_VERSION(ahd_pci, 1);

static int
ahd_pci_probe(device_t dev)
{
	struct	ahd_pci_identity *entry;

	entry = ahd_find_pci_device(dev);
	if (entry != NULL) {
		device_set_desc(dev, entry->name);
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
ahd_pci_attach(device_t dev)
{
	struct	 ahd_pci_identity *entry;
	struct	 ahd_softc *ahd;
	char	*name;
	int	 error;

	entry = ahd_find_pci_device(dev);
	if (entry == NULL)
		return (ENXIO);

	/*
	 * Allocate a softc for this card and
	 * set it up for attachment by our
	 * common detect routine.
	 */
	name = malloc(strlen(device_get_nameunit(dev)) + 1, M_DEVBUF, M_NOWAIT);
	if (name == NULL)
		return (ENOMEM);
	strcpy(name, device_get_nameunit(dev));
	ahd = ahd_alloc(dev, name);
	if (ahd == NULL)
		return (ENOMEM);

	ahd_set_unit(ahd, device_get_unit(dev));

	/*
	 * Should we bother disabling 39Bit addressing
	 * based on installed memory?
	 */
	if (sizeof(bus_addr_t) > 4)
                ahd->flags |= AHD_39BIT_ADDRESSING;

	/* Allocate a dmatag for our SCB DMA maps */
	error = aic_dma_tag_create(ahd, /*parent*/bus_get_dma_tag(dev),
				   /*alignment*/1, /*boundary*/0,
				   (ahd->flags & AHD_39BIT_ADDRESSING)
				   ? 0x7FFFFFFFFF
				   : BUS_SPACE_MAXADDR_32BIT,
				   /*highaddr*/BUS_SPACE_MAXADDR,
				   /*filter*/NULL, /*filterarg*/NULL,
				   /*maxsize*/BUS_SPACE_MAXSIZE_32BIT,
				   /*nsegments*/AHD_NSEG,
				   /*maxsegsz*/AHD_MAXTRANSFER_SIZE,
				   /*flags*/0,
				   &ahd->parent_dmat);

	if (error != 0) {
		printf("ahd_pci_attach: Could not allocate DMA tag "
		       "- error %d\n", error);
		ahd_free(ahd);
		return (ENOMEM);
	}
	ahd->dev_softc = dev;
	error = ahd_pci_config(ahd, entry);
	if (error != 0) {
		ahd_free(ahd);
		return (error);
	}

	ahd_sysctl(ahd);
	ahd_attach(ahd);
	return (0);
}

int
ahd_pci_map_registers(struct ahd_softc *ahd)
{
	struct	resource *regs;
	struct	resource *regs2;
	int	regs_type;
	int	regs_id;
	int	regs_id2;
	int	allow_memio;

	regs = NULL;
	regs2 = NULL;
	regs_type = 0;
	regs_id = 0;

	/* Retrieve the per-device 'allow_memio' hint */
	if (resource_int_value(device_get_name(ahd->dev_softc),
			       device_get_unit(ahd->dev_softc),
			       "allow_memio", &allow_memio) != 0) {
		if (bootverbose)
			device_printf(ahd->dev_softc,
				      "Defaulting to MEMIO on\n");
		allow_memio = 1;
	}

	if ((ahd->bugs & AHD_PCIX_MMAPIO_BUG) == 0
	 && allow_memio != 0) {

		regs_type = SYS_RES_MEMORY;
		regs_id = AHD_PCI_MEMADDR;
		regs = bus_alloc_resource_any(ahd->dev_softc, regs_type,
					      &regs_id, RF_ACTIVE);
		if (regs != NULL) {
			int error;

			ahd->tags[0] = rman_get_bustag(regs);
			ahd->bshs[0] = rman_get_bushandle(regs);
			ahd->tags[1] = ahd->tags[0];
			error = bus_space_subregion(ahd->tags[0], ahd->bshs[0],
						    /*offset*/0x100,
						    /*size*/0x100,
						    &ahd->bshs[1]);
			/*
			 * Do a quick test to see if memory mapped
			 * I/O is functioning correctly.
			 */
			if (error != 0
			 || ahd_pci_test_register_access(ahd) != 0) {
				device_printf(ahd->dev_softc,
				       "PCI Device %d:%d:%d failed memory "
				       "mapped test.  Using PIO.\n",
				       aic_get_pci_bus(ahd->dev_softc),
				       aic_get_pci_slot(ahd->dev_softc),
				       aic_get_pci_function(ahd->dev_softc));
				bus_release_resource(ahd->dev_softc, regs_type,
						     regs_id, regs);
				regs = NULL;
				AHD_CORRECTABLE_ERROR(ahd);
			}
		}
	}
	if (regs == NULL) {
		regs_type = SYS_RES_IOPORT;
		regs_id = AHD_PCI_IOADDR0;
		regs = bus_alloc_resource_any(ahd->dev_softc, regs_type,
					      &regs_id, RF_ACTIVE);
		if (regs == NULL) {
			device_printf(ahd->dev_softc,
				      "can't allocate register resources\n");
			AHD_UNCORRECTABLE_ERROR(ahd);
			return (ENOMEM);
		}
		ahd->tags[0] = rman_get_bustag(regs);
		ahd->bshs[0] = rman_get_bushandle(regs);

		/* And now the second BAR */
		regs_id2 = AHD_PCI_IOADDR1;
		regs2 = bus_alloc_resource_any(ahd->dev_softc, regs_type,
					       &regs_id2, RF_ACTIVE);
		if (regs2 == NULL) {
			device_printf(ahd->dev_softc,
				      "can't allocate register resources\n");
			AHD_UNCORRECTABLE_ERROR(ahd);
			return (ENOMEM);
		}
		ahd->tags[1] = rman_get_bustag(regs2);
		ahd->bshs[1] = rman_get_bushandle(regs2);
		ahd->platform_data->regs_res_type[1] = regs_type;
		ahd->platform_data->regs_res_id[1] = regs_id2;
		ahd->platform_data->regs[1] = regs2;
	}
	ahd->platform_data->regs_res_type[0] = regs_type;
	ahd->platform_data->regs_res_id[0] = regs_id;
	ahd->platform_data->regs[0] = regs;
	return (0);
}

int
ahd_pci_map_int(struct ahd_softc *ahd)
{
	int zero;

	zero = 0;
	ahd->platform_data->irq =
	    bus_alloc_resource_any(ahd->dev_softc, SYS_RES_IRQ, &zero,
				   RF_ACTIVE | RF_SHAREABLE);
	if (ahd->platform_data->irq == NULL)
		return (ENOMEM);
	ahd->platform_data->irq_res_type = SYS_RES_IRQ;
	return (ahd_map_int(ahd));
}
