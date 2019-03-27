/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2003 by Peter Grehan. All rights reserved.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/rman.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofwpci.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/pio.h>
#include <machine/resource.h>

#include <powerpc/powermac/gracklevar.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include "pcib_if.h"

/*
 * Device interface.
 */
static int		grackle_probe(device_t);
static int		grackle_attach(device_t);

/*
 * pcib interface.
 */
static u_int32_t	grackle_read_config(device_t, u_int, u_int, u_int,
			    u_int, int);
static void		grackle_write_config(device_t, u_int, u_int, u_int,
			    u_int, u_int32_t, int);

/*
 * Local routines.
 */
static int		grackle_enable_config(struct grackle_softc *, u_int,
			    u_int, u_int, u_int);
static void		grackle_disable_config(struct grackle_softc *);
static int		badaddr(void *, size_t);

/*
 * Driver methods.
 */
static device_method_t	grackle_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		grackle_probe),
	DEVMETHOD(device_attach,	grackle_attach),

	/* pcib interface */
	DEVMETHOD(pcib_read_config,	grackle_read_config),
	DEVMETHOD(pcib_write_config,	grackle_write_config),

	DEVMETHOD_END
};

static devclass_t	grackle_devclass;
DEFINE_CLASS_1(pcib, grackle_driver, grackle_methods,
    sizeof(struct grackle_softc), ofw_pci_driver);
DRIVER_MODULE(grackle, ofwbus, grackle_driver, grackle_devclass, 0, 0);

static int
grackle_probe(device_t dev)
{
	const char	*type, *compatible;

	type = ofw_bus_get_type(dev);
	compatible = ofw_bus_get_compat(dev);

	if (type == NULL || compatible == NULL)
		return (ENXIO);

	if (strcmp(type, "pci") != 0 || strcmp(compatible, "grackle") != 0)
		return (ENXIO);

	device_set_desc(dev, "MPC106 (Grackle) Host-PCI bridge");
	return (0);
}

static int
grackle_attach(device_t dev)
{
	struct		grackle_softc *sc;

	sc = device_get_softc(dev);

	/*
	 * The Grackle PCI config addr/data registers are actually in
	 * PCI space, but since they are needed to actually probe the
	 * PCI bus, use the fact that they are also available directly
	 * on the processor bus and map them
	 */
	sc->sc_addr = (vm_offset_t)pmap_mapdev(GRACKLE_ADDR, PAGE_SIZE);
	sc->sc_data = (vm_offset_t)pmap_mapdev(GRACKLE_DATA, PAGE_SIZE);

	return (ofw_pci_attach(dev));
}

static u_int32_t
grackle_read_config(device_t dev, u_int bus, u_int slot, u_int func, u_int reg,
    int width)
{
	struct		grackle_softc *sc;
	vm_offset_t	caoff;
	u_int32_t	retval = 0xffffffff;

	sc = device_get_softc(dev);
	caoff = sc->sc_data + (reg & 0x03);

	if (grackle_enable_config(sc, bus, slot, func, reg) != 0) {

		/*
		 * Config probes to non-existent devices on the
		 * secondary bus generates machine checks. Be sure
		 * to catch these.
		 */
		if (bus > 0) {
		  if (badaddr((void *)sc->sc_data, 4)) {
			  return (retval);
		  }
		}

		switch (width) {
		case 1:
			retval = (in8rb(caoff));
			break;
		case 2:
			retval = (in16rb(caoff));
			break;
		case 4:
			retval = (in32rb(caoff));
			break;
		}
	}
	grackle_disable_config(sc);

	return (retval);
}

static void
grackle_write_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, u_int32_t val, int width)
{
	struct		grackle_softc *sc;
	vm_offset_t	caoff;

	sc = device_get_softc(dev);
	caoff = sc->sc_data + (reg & 0x03);

	if (grackle_enable_config(sc, bus, slot, func, reg)) {
		switch (width) {
		case 1:
			out8rb(caoff, val);
			(void)in8rb(caoff);
			break;
		case 2:
			out16rb(caoff, val);
			(void)in16rb(caoff);
			break;
		case 4:
			out32rb(caoff, val);
			(void)in32rb(caoff);
			break;
		}
	}
	grackle_disable_config(sc);
}

static int
grackle_enable_config(struct grackle_softc *sc, u_int bus, u_int slot,
    u_int func, u_int reg)
{
	u_int32_t	cfgval;

	/*
	 * Unlike UniNorth, the format of the config word is the same
	 * for local (0) and remote busses.
	 */
	cfgval = (bus << 16) | (slot << 11) | (func << 8) | (reg & 0xFC)
	    | GRACKLE_CFG_ENABLE;

	out32rb(sc->sc_addr, cfgval);
	(void) in32rb(sc->sc_addr);

	return (1);
}

static void
grackle_disable_config(struct grackle_softc *sc)
{
	/*
	 * Clear the GRACKLE_CFG_ENABLE bit to prevent stray
	 * accesses from causing config cycles
	 */
	out32rb(sc->sc_addr, 0);
}

static int
badaddr(void *addr, size_t size)
{
	struct thread	*td;
	jmp_buf		env, *oldfaultbuf;
	int		x;

	/* Get rid of any stale machine checks that have been waiting.  */
	__asm __volatile ("sync; isync");

	td = curthread;

	oldfaultbuf = td->td_pcb->pcb_onfault;
	td->td_pcb->pcb_onfault = &env;
	if (setjmp(env)) {
		td->td_pcb->pcb_onfault = oldfaultbuf;
		__asm __volatile ("sync");
		return 1;
	}

	__asm __volatile ("sync");

	switch (size) {
	case 1:
		x = *(volatile int8_t *)addr;
		break;
	case 2:
		x = *(volatile int16_t *)addr;
		break;
	case 4:
		x = *(volatile int32_t *)addr;
		break;
	default:
		panic("badaddr: invalid size (%zd)", size);
	}

	/* Make sure we took the machine check, if we caused one. */
	__asm __volatile ("sync; isync");

	td->td_pcb->pcb_onfault = oldfaultbuf;
	__asm __volatile ("sync");	/* To be sure. */

	return (0);
}

/*
 * Driver to swallow Grackle host bridges from the PCI bus side.
 */
static int
grackle_hb_probe(device_t dev)
{

	if (pci_get_devid(dev) == 0x00021057) {
		device_set_desc(dev, "Grackle Host to PCI bridge");
		device_quiet(dev);
		return (0);
	}

	return (ENXIO);
}

static int
grackle_hb_attach(device_t dev)
{

	return (0);
}

static device_method_t grackle_hb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         grackle_hb_probe),
	DEVMETHOD(device_attach,        grackle_hb_attach),

	{ 0, 0 }
};

static driver_t grackle_hb_driver = {
	"grackle_hb",
	grackle_hb_methods,
	1,
};
static devclass_t grackle_hb_devclass;

DRIVER_MODULE(grackle_hb, pci, grackle_hb_driver, grackle_hb_devclass, 0, 0);
