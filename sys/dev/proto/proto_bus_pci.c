/*-
 * Copyright (c) 2014, 2015 Marcel Moolenaar
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
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>
#include <sys/sbuf.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/proto/proto.h>

static int proto_pci_probe(device_t dev);
static int proto_pci_attach(device_t dev);

static device_method_t proto_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		proto_pci_probe),
	DEVMETHOD(device_attach,	proto_pci_attach),
	DEVMETHOD(device_detach,	proto_detach),
	DEVMETHOD_END
};

static driver_t proto_pci_driver = {
	proto_driver_name,
	proto_pci_methods,
	sizeof(struct proto_softc),
};

static char proto_pci_prefix[] = "pci";
static char **proto_pci_devnames;

static int
proto_pci_probe(device_t dev)
{
	struct sbuf *sb;

	if ((pci_read_config(dev, PCIR_HDRTYPE, 1) & PCIM_HDRTYPE) != 0)
		return (ENXIO);

	sb = sbuf_new_auto();
	sbuf_printf(sb, "%s%d:%d:%d:%d", proto_pci_prefix, pci_get_domain(dev),
	    pci_get_bus(dev), pci_get_slot(dev), pci_get_function(dev));
	sbuf_finish(sb);
	device_set_desc_copy(dev, sbuf_data(sb));
	sbuf_delete(sb);
	return (proto_probe(dev, proto_pci_prefix, &proto_pci_devnames));
}

static int
proto_pci_attach(device_t dev)
{
	struct proto_softc *sc;
	struct resource *res;
	uint32_t val;
	int bar, rid, type;

	sc = device_get_softc(dev);

	proto_add_resource(sc, PROTO_RES_PCICFG, 0, NULL);
	proto_add_resource(sc, PROTO_RES_BUSDMA, 0, NULL);

	for (bar = 0; bar < PCIR_MAX_BAR_0; bar++) {
		rid = PCIR_BAR(bar);
		val = pci_read_config(dev, rid, 4);
		type = (PCI_BAR_IO(val)) ? SYS_RES_IOPORT : SYS_RES_MEMORY;
		res = bus_alloc_resource_any(dev, type, &rid, RF_ACTIVE);
		if (res == NULL)
			continue;
		proto_add_resource(sc, type, rid, res);
		if (type == SYS_RES_IOPORT)
			continue;
		/* Skip over adjacent BAR for 64-bit memory BARs. */
		if ((val & PCIM_BAR_MEM_TYPE) == PCIM_BAR_MEM_64)
			bar++;
	}

	rid = 0;
	type = SYS_RES_IRQ;
	res = bus_alloc_resource_any(dev, type, &rid, RF_ACTIVE | RF_SHAREABLE);
	if (res != NULL)
		proto_add_resource(sc, type, rid, res);
	return (proto_attach(dev));
}

DRIVER_MODULE(proto, pci, proto_pci_driver, proto_devclass, NULL, NULL);
