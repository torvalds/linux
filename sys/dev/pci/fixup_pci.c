/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1994,1995 Stefan Esser, Wolfgang StanglMeier
 * Copyright (c) 2000 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2000 BSDi
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

/*
 * Chipset fixups.
 *
 * These routines are invoked during the probe phase for devices which
 * typically don't have specific device drivers, but which require
 * some cleaning up.
 */

static int	fixup_pci_probe(device_t dev);
static void	fixwsc_natoma(device_t dev);
static void	fixc1_nforce2(device_t dev);

static device_method_t fixup_pci_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,		fixup_pci_probe),
    DEVMETHOD(device_attach,		bus_generic_attach),
    { 0, 0 }
};

static driver_t fixup_pci_driver = {
    "fixup_pci",
    fixup_pci_methods,
    0,
};

static devclass_t fixup_pci_devclass;

DRIVER_MODULE(fixup_pci, pci, fixup_pci_driver, fixup_pci_devclass, 0, 0);

static int
fixup_pci_probe(device_t dev)
{
    switch (pci_get_devid(dev)) {
    case 0x12378086:		/* Intel 82440FX (Natoma) */
	fixwsc_natoma(dev);
	break;
    case 0x01e010de:		/* nVidia nForce2 */
	fixc1_nforce2(dev);
	break;
    }
    return(ENXIO);
}

static void
fixwsc_natoma(device_t dev)
{
    int		pmccfg;

    pmccfg = pci_read_config(dev, 0x50, 2);
#if defined(SMP)
    if (pmccfg & 0x8000) {
	device_printf(dev, "correcting Natoma config for SMP\n");
	pmccfg &= ~0x8000;
	pci_write_config(dev, 0x50, pmccfg, 2);
    }
#else
    if ((pmccfg & 0x8000) == 0) {
	device_printf(dev, "correcting Natoma config for non-SMP\n");
	pmccfg |= 0x8000;
	pci_write_config(dev, 0x50, pmccfg, 2);
    }
#endif
}

/*
 * Set the SYSTEM_IDLE_TIMEOUT to 80 ns on nForce2 systems to work
 * around a hang that is triggered when the CPU generates a very fast
 * CONNECT/HALT cycle sequence.  Specifically, the hang can result in
 * the lapic timer being stopped.
 *
 * This requires changing the value for config register at offset 0x6c
 * for the Host-PCI bridge at bus/dev/function 0/0/0:
 *
 * Chip	Current Value	New Value
 * ----	----------	----------
 * C17	0x1F0FFF01	0x1F01FF01
 * C18D	0x9F0FFF01	0x9F01FF01
 *
 * We do this by always clearing the bits in 0x000e0000.
 *
 * See also: http://lkml.org/lkml/2004/5/3/157
 */
static void
fixc1_nforce2(device_t dev)
{
	uint32_t val;

	if (pci_get_bus(dev) == 0 && pci_get_slot(dev) == 0 &&
	    pci_get_function(dev) == 0) {
		val = pci_read_config(dev, 0x6c, 4);
		if (val & 0x000e0000) {
			device_printf(dev, 
			    "correcting nForce2 C1 CPU disconnect hangs\n");
			val &= ~0x000e0000;
			pci_write_config(dev, 0x6c, val, 4);
		}
	}
}
