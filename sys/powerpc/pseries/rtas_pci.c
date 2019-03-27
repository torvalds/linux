/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Nathan Whitehorn
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
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
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
#include <machine/rtas.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <powerpc/pseries/plpar_iommu.h>

#include "pcib_if.h"
#include "iommu_if.h"

/*
 * Device interface.
 */
static int		rtaspci_probe(device_t);
static int		rtaspci_attach(device_t);

/*
 * pcib interface.
 */
static u_int32_t	rtaspci_read_config(device_t, u_int, u_int, u_int,
			    u_int, int);
static void		rtaspci_write_config(device_t, u_int, u_int, u_int,
			    u_int, u_int32_t, int);

/*
 * Driver methods.
 */
static device_method_t	rtaspci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rtaspci_probe),
	DEVMETHOD(device_attach,	rtaspci_attach),

	/* pcib interface */
	DEVMETHOD(pcib_read_config,	rtaspci_read_config),
	DEVMETHOD(pcib_write_config,	rtaspci_write_config),

	DEVMETHOD_END
};

struct rtaspci_softc {
	struct ofw_pci_softc	pci_sc;

	struct ofw_pci_register	sc_pcir;

	cell_t			read_pci_config, write_pci_config;
	cell_t			ex_read_pci_config, ex_write_pci_config;
	int			sc_extended_config;
};

static devclass_t	rtaspci_devclass;
DEFINE_CLASS_1(pcib, rtaspci_driver, rtaspci_methods,
    sizeof(struct rtaspci_softc), ofw_pci_driver);
DRIVER_MODULE(rtaspci, ofwbus, rtaspci_driver, rtaspci_devclass, 0, 0);

static int
rtaspci_probe(device_t dev)
{
	const char	*type;

	if (!rtas_exists())
		return (ENXIO);

	type = ofw_bus_get_type(dev);

	if (OF_getproplen(ofw_bus_get_node(dev), "used-by-rtas") < 0)
		return (ENXIO);
	if (type == NULL || strcmp(type, "pci") != 0)
		return (ENXIO);

	device_set_desc(dev, "RTAS Host-PCI bridge");
	return (BUS_PROBE_GENERIC);
}

static int
rtaspci_attach(device_t dev)
{
	struct		rtaspci_softc *sc;

	sc = device_get_softc(dev);

	if (OF_getencprop(ofw_bus_get_node(dev), "reg", (pcell_t *)&sc->sc_pcir,
	    sizeof(sc->sc_pcir)) == -1)
		return (ENXIO);

	sc->read_pci_config = rtas_token_lookup("read-pci-config");
	sc->write_pci_config = rtas_token_lookup("write-pci-config");
	sc->ex_read_pci_config = rtas_token_lookup("ibm,read-pci-config");
	sc->ex_write_pci_config = rtas_token_lookup("ibm,write-pci-config");

	sc->sc_extended_config = 0;
	OF_getencprop(ofw_bus_get_node(dev), "ibm,pci-config-space-type",
	    &sc->sc_extended_config, sizeof(sc->sc_extended_config));

	return (ofw_pci_attach(dev));
}

static uint32_t
rtaspci_read_config(device_t dev, u_int bus, u_int slot, u_int func, u_int reg,
    int width)
{
	struct rtaspci_softc *sc;
	uint32_t retval = 0xffffffff;
	uint32_t config_addr;
	int error, pcierror;

	sc = device_get_softc(dev);
	
	config_addr = ((bus & 0xff) << 16) | ((slot & 0x1f) << 11) |
	    ((func & 0x7) << 8) | (reg & 0xff);
	if (sc->sc_extended_config)
		config_addr |= (reg & 0xf00) << 16;
		
	if (sc->ex_read_pci_config != -1)
		error = rtas_call_method(sc->ex_read_pci_config, 4, 2,
		    config_addr, sc->sc_pcir.phys_hi,
		    sc->sc_pcir.phys_mid, width, &pcierror, &retval);
	else
		error = rtas_call_method(sc->read_pci_config, 2, 2,
		    config_addr, width, &pcierror, &retval);

	/* Sign-extend output */
	switch (width) {
	case 1:
		retval = (int32_t)(int8_t)(retval);
		break;
	case 2:
		retval = (int32_t)(int16_t)(retval);
		break;
	}
	
	if (error < 0 || pcierror != 0)
		retval = 0xffffffff;

	return (retval);
}

static void
rtaspci_write_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, uint32_t val, int width)
{
	struct rtaspci_softc *sc;
	uint32_t config_addr;
	int pcierror;

	sc = device_get_softc(dev);
	
	config_addr = ((bus & 0xff) << 16) | ((slot & 0x1f) << 11) |
	    ((func & 0x7) << 8) | (reg & 0xff);
	if (sc->sc_extended_config)
		config_addr |= (reg & 0xf00) << 16;
		
	if (sc->ex_write_pci_config != -1)
		rtas_call_method(sc->ex_write_pci_config, 5, 1, config_addr,
		    sc->sc_pcir.phys_hi, sc->sc_pcir.phys_mid,
		    width, val, &pcierror);
	else
		rtas_call_method(sc->write_pci_config, 3, 1, config_addr,
		    width, val, &pcierror);
}

