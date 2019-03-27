/*-
 * Copyright (c) 2015-2016 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <dev/pci/pcivar.h>

#ifdef PCI_IOV
#include <sys/nv.h>
#include <sys/iov_schema.h>
#include <dev/pci/pci_iov.h>
#endif

#include "common/common.h"
#include "t4_if.h"

struct t4iov_softc {
	device_t sc_dev;
	device_t sc_main;
	bool sc_attached;
};

struct {
	uint16_t device;
	char *desc;
} t4iov_pciids[] = {
	{0x4000, "Chelsio T440-dbg"},
	{0x4001, "Chelsio T420-CR"},
	{0x4002, "Chelsio T422-CR"},
	{0x4003, "Chelsio T440-CR"},
	{0x4004, "Chelsio T420-BCH"},
	{0x4005, "Chelsio T440-BCH"},
	{0x4006, "Chelsio T440-CH"},
	{0x4007, "Chelsio T420-SO"},
	{0x4008, "Chelsio T420-CX"},
	{0x4009, "Chelsio T420-BT"},
	{0x400a, "Chelsio T404-BT"},
	{0x400e, "Chelsio T440-LP-CR"},
}, t5iov_pciids[] = {
	{0x5000, "Chelsio T580-dbg"},
	{0x5001,  "Chelsio T520-CR"},		/* 2 x 10G */
	{0x5002,  "Chelsio T522-CR"},		/* 2 x 10G, 2 X 1G */
	{0x5003,  "Chelsio T540-CR"},		/* 4 x 10G */
	{0x5007,  "Chelsio T520-SO"},		/* 2 x 10G, nomem */
	{0x5009,  "Chelsio T520-BT"},		/* 2 x 10GBaseT */
	{0x500a,  "Chelsio T504-BT"},		/* 4 x 1G */
	{0x500d,  "Chelsio T580-CR"},		/* 2 x 40G */
	{0x500e,  "Chelsio T540-LP-CR"},	/* 4 x 10G */
	{0x5010,  "Chelsio T580-LP-CR"},	/* 2 x 40G */
	{0x5011,  "Chelsio T520-LL-CR"},	/* 2 x 10G */
	{0x5012,  "Chelsio T560-CR"},		/* 1 x 40G, 2 x 10G */
	{0x5014,  "Chelsio T580-LP-SO-CR"},	/* 2 x 40G, nomem */
	{0x5015,  "Chelsio T502-BT"},		/* 2 x 1G */
#ifdef notyet
	{0x5004,  "Chelsio T520-BCH"},
	{0x5005,  "Chelsio T540-BCH"},
	{0x5006,  "Chelsio T540-CH"},
	{0x5008,  "Chelsio T520-CX"},
	{0x500b,  "Chelsio B520-SR"},
	{0x500c,  "Chelsio B504-BT"},
	{0x500f,  "Chelsio Amsterdam"},
	{0x5013,  "Chelsio T580-CHR"},
#endif
}, t6iov_pciids[] = {
	{0x6000, "Chelsio T6-DBG-25"},		/* 2 x 10/25G, debug */
	{0x6001, "Chelsio T6225-CR"},		/* 2 x 10/25G */
	{0x6002, "Chelsio T6225-SO-CR"},	/* 2 x 10/25G, nomem */
	{0x6003, "Chelsio T6425-CR"},		/* 4 x 10/25G */
	{0x6004, "Chelsio T6425-SO-CR"},	/* 4 x 10/25G, nomem */
	{0x6005, "Chelsio T6225-OCP-SO"},	/* 2 x 10/25G, nomem */
	{0x6006, "Chelsio T62100-OCP-SO"},	/* 2 x 40/50/100G, nomem */
	{0x6007, "Chelsio T62100-LP-CR"},	/* 2 x 40/50/100G */
	{0x6008, "Chelsio T62100-SO-CR"},	/* 2 x 40/50/100G, nomem */
	{0x6009, "Chelsio T6210-BT"},		/* 2 x 10GBASE-T */
	{0x600d, "Chelsio T62100-CR"},		/* 2 x 40/50/100G */
	{0x6010, "Chelsio T6-DBG-100"},		/* 2 x 40/50/100G, debug */
	{0x6011, "Chelsio T6225-LL-CR"},	/* 2 x 10/25G */
	{0x6014, "Chelsio T61100-OCP-SO"},	/* 1 x 40/50/100G, nomem */
	{0x6015, "Chelsio T6201-BT"},		/* 2 x 1000BASE-T */

	/* Custom */
	{0x6080, "Chelsio T6225 80"},
	{0x6081, "Chelsio T62100 81"},
};

static int	t4iov_attach_child(device_t dev);

static int
t4iov_probe(device_t dev)
{
	uint16_t d;
	size_t i;

	if (pci_get_vendor(dev) != PCI_VENDOR_ID_CHELSIO)
		return (ENXIO);

	d = pci_get_device(dev);
	for (i = 0; i < nitems(t4iov_pciids); i++) {
		if (d == t4iov_pciids[i].device) {
			device_set_desc(dev, t4iov_pciids[i].desc);
			device_quiet(dev);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
t5iov_probe(device_t dev)
{
	uint16_t d;
	size_t i;

	if (pci_get_vendor(dev) != PCI_VENDOR_ID_CHELSIO)
		return (ENXIO);

	d = pci_get_device(dev);
	for (i = 0; i < nitems(t5iov_pciids); i++) {
		if (d == t5iov_pciids[i].device) {
			device_set_desc(dev, t5iov_pciids[i].desc);
			device_quiet(dev);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
t6iov_probe(device_t dev)
{
	uint16_t d;
	size_t i;

	if (pci_get_vendor(dev) != PCI_VENDOR_ID_CHELSIO)
		return (ENXIO);

	d = pci_get_device(dev);
	for (i = 0; i < nitems(t6iov_pciids); i++) {
		if (d == t6iov_pciids[i].device) {
			device_set_desc(dev, t6iov_pciids[i].desc);
			device_quiet(dev);
			return (BUS_PROBE_DEFAULT);
		}
	}
	return (ENXIO);
}

static int
t4iov_attach(device_t dev)
{
	struct t4iov_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	sc->sc_main = pci_find_dbsf(pci_get_domain(dev), pci_get_bus(dev),
	    pci_get_slot(dev), 4);
	if (sc->sc_main == NULL)
		return (ENXIO);
	if (T4_IS_MAIN_READY(sc->sc_main) == 0)
		return (t4iov_attach_child(dev));
	return (0);
}

static int
t4iov_attach_child(device_t dev)
{
	struct t4iov_softc *sc;
#ifdef PCI_IOV
	nvlist_t *pf_schema, *vf_schema;
#endif
	device_t pdev;
	int error;

	sc = device_get_softc(dev);
	MPASS(!sc->sc_attached);

	/*
	 * PF0-3 are associated with a specific port on the NIC (PF0
	 * with port 0, etc.).  Ask the PF4 driver for the device for
	 * this function's associated port to determine if the port is
	 * present.
	 */
	error = T4_READ_PORT_DEVICE(sc->sc_main, pci_get_function(dev), &pdev);
	if (error)
		return (0);

#ifdef PCI_IOV
	pf_schema = pci_iov_schema_alloc_node();
	vf_schema = pci_iov_schema_alloc_node();
	error = pci_iov_attach_name(dev, pf_schema, vf_schema, "%s",
	    device_get_nameunit(pdev));
	if (error) {
		device_printf(dev, "Failed to initialize SR-IOV: %d\n", error);
		return (0);
	}
#endif

	sc->sc_attached = true;
	return (0);
}

static int
t4iov_detach_child(device_t dev)
{
	struct t4iov_softc *sc;
#ifdef PCI_IOV
	int error;
#endif

	sc = device_get_softc(dev);
	if (!sc->sc_attached)
		return (0);

#ifdef PCI_IOV
	error = pci_iov_detach(dev);
	if (error != 0) {
		device_printf(dev, "Failed to disable SR-IOV\n");
		return (error);
	}
#endif

	sc->sc_attached = false;
	return (0);
}

static int
t4iov_detach(device_t dev)
{
	struct t4iov_softc *sc;
	int error;

	sc = device_get_softc(dev);
	if (sc->sc_attached) {
		error = t4iov_detach_child(dev);
		if (error)
			return (error);
	}
	return (0);
}

#ifdef PCI_IOV
static int
t4iov_iov_init(device_t dev, uint16_t num_vfs, const struct nvlist *config)
{

	/* XXX: The Linux driver sets up a vf_monitor task on T4 adapters. */
	return (0);
}

static void
t4iov_iov_uninit(device_t dev)
{
}

static int
t4iov_add_vf(device_t dev, uint16_t vfnum, const struct nvlist *config)
{

	return (0);
}
#endif

static device_method_t t4iov_methods[] = {
	DEVMETHOD(device_probe,		t4iov_probe),
	DEVMETHOD(device_attach,	t4iov_attach),
	DEVMETHOD(device_detach,	t4iov_detach),

#ifdef PCI_IOV
	DEVMETHOD(pci_iov_init,		t4iov_iov_init),
	DEVMETHOD(pci_iov_uninit,	t4iov_iov_uninit),
	DEVMETHOD(pci_iov_add_vf,	t4iov_add_vf),
#endif

	DEVMETHOD(t4_attach_child,	t4iov_attach_child),
	DEVMETHOD(t4_detach_child,	t4iov_detach_child),

	DEVMETHOD_END
};

static driver_t t4iov_driver = {
	"t4iov",
	t4iov_methods,
	sizeof(struct t4iov_softc)
};

static device_method_t t5iov_methods[] = {
	DEVMETHOD(device_probe,		t5iov_probe),
	DEVMETHOD(device_attach,	t4iov_attach),
	DEVMETHOD(device_detach,	t4iov_detach),

#ifdef PCI_IOV
	DEVMETHOD(pci_iov_init,		t4iov_iov_init),
	DEVMETHOD(pci_iov_uninit,	t4iov_iov_uninit),
	DEVMETHOD(pci_iov_add_vf,	t4iov_add_vf),
#endif

	DEVMETHOD(t4_attach_child,	t4iov_attach_child),
	DEVMETHOD(t4_detach_child,	t4iov_detach_child),

	DEVMETHOD_END
};

static driver_t t5iov_driver = {
	"t5iov",
	t5iov_methods,
	sizeof(struct t4iov_softc)
};

static device_method_t t6iov_methods[] = {
	DEVMETHOD(device_probe,		t6iov_probe),
	DEVMETHOD(device_attach,	t4iov_attach),
	DEVMETHOD(device_detach,	t4iov_detach),

#ifdef PCI_IOV
	DEVMETHOD(pci_iov_init,		t4iov_iov_init),
	DEVMETHOD(pci_iov_uninit,	t4iov_iov_uninit),
	DEVMETHOD(pci_iov_add_vf,	t4iov_add_vf),
#endif

	DEVMETHOD(t4_attach_child,	t4iov_attach_child),
	DEVMETHOD(t4_detach_child,	t4iov_detach_child),

	DEVMETHOD_END
};

static driver_t t6iov_driver = {
	"t6iov",
	t6iov_methods,
	sizeof(struct t4iov_softc)
};

static devclass_t t4iov_devclass, t5iov_devclass, t6iov_devclass;

DRIVER_MODULE(t4iov, pci, t4iov_driver, t4iov_devclass, 0, 0);
MODULE_VERSION(t4iov, 1);

DRIVER_MODULE(t5iov, pci, t5iov_driver, t5iov_devclass, 0, 0);
MODULE_VERSION(t5iov, 1);

DRIVER_MODULE(t6iov, pci, t6iov_driver, t6iov_devclass, 0, 0);
MODULE_VERSION(t6iov, 1);
