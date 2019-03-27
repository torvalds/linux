/*-
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * BHNDB PCI SPROM driver.
 * 
 * Provides support for early PCI bridge cores that vend SPROM CSRs
 * via PCI configuration space.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/bhnd/bhnd.h>
#include <dev/bhnd/cores/pci/bhnd_pci_hostbvar.h>
#include <dev/bhnd/nvram/bhnd_spromvar.h>

#include "bhnd_nvram_if.h"

#include "bhndb_pcireg.h"
#include "bhndb_pcivar.h"

static int
bhndb_pci_sprom_probe(device_t dev)
{
	device_t	bridge;
	int		error;

	/* Our parent must be a PCI-BHND bridge */
	bridge = device_get_parent(dev);
	if (device_get_driver(bridge) != &bhndb_pci_driver)
		return (ENXIO);

	/* Defer to default driver implementation */
	if ((error = bhnd_sprom_probe(dev)) > 0)
		return (error);

	return (BUS_PROBE_NOWILDCARD);
}


static device_method_t bhndb_pci_sprom_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			bhndb_pci_sprom_probe),
	DEVMETHOD_END
};

DEFINE_CLASS_1(bhnd_nvram, bhndb_pci_sprom_driver, bhndb_pci_sprom_methods, sizeof(struct bhnd_sprom_softc), bhnd_sprom_driver);

DRIVER_MODULE(bhndb_pci_sprom, bhndb, bhndb_pci_sprom_driver, bhnd_nvram_devclass, NULL, NULL);
MODULE_DEPEND(bhndb_pci_sprom, bhnd, 1, 1, 1);
MODULE_DEPEND(bhndb_pci_sprom, bhnd_sprom, 1, 1, 1);
MODULE_VERSION(bhndb_pci_sprom, 1);
