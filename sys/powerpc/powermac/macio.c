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
 *
 * $FreeBSD$
 */

/*
 * Driver for KeyLargo/Pangea, the MacPPC south bridge ASIC.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/resource.h>
#include <machine/vmparam.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <powerpc/powermac/maciovar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

/*
 * Macio softc
 */
struct macio_softc {
	phandle_t    sc_node;
	vm_offset_t  sc_base;
	vm_offset_t  sc_size;
	struct rman  sc_mem_rman;

	/* FCR registers */
	int          sc_memrid;
	struct resource	*sc_memr;
};

static MALLOC_DEFINE(M_MACIO, "macio", "macio device information");

static int  macio_probe(device_t);
static int  macio_attach(device_t);
static int  macio_print_child(device_t dev, device_t child);
static void macio_probe_nomatch(device_t, device_t);
static struct   resource *macio_alloc_resource(device_t, device_t, int, int *,
					       rman_res_t, rman_res_t, rman_res_t,
					       u_int);
static int  macio_activate_resource(device_t, device_t, int, int,
				    struct resource *);
static int  macio_deactivate_resource(device_t, device_t, int, int,
				      struct resource *);
static int  macio_release_resource(device_t, device_t, int, int,
				   struct resource *);
static struct resource_list *macio_get_resource_list (device_t, device_t);
static ofw_bus_get_devinfo_t macio_get_devinfo;

/*
 * Bus interface definition
 */
static device_method_t macio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         macio_probe),
	DEVMETHOD(device_attach,        macio_attach),
	DEVMETHOD(device_detach,        bus_generic_detach),
	DEVMETHOD(device_shutdown,      bus_generic_shutdown),
	DEVMETHOD(device_suspend,       bus_generic_suspend),
	DEVMETHOD(device_resume,        bus_generic_resume),
	
	/* Bus interface */
	DEVMETHOD(bus_print_child,      macio_print_child),
	DEVMETHOD(bus_probe_nomatch,    macio_probe_nomatch),
	DEVMETHOD(bus_setup_intr,       bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,    bus_generic_teardown_intr),	

        DEVMETHOD(bus_alloc_resource,   macio_alloc_resource),
        DEVMETHOD(bus_release_resource, macio_release_resource),
        DEVMETHOD(bus_activate_resource, macio_activate_resource),
        DEVMETHOD(bus_deactivate_resource, macio_deactivate_resource),
        DEVMETHOD(bus_get_resource_list, macio_get_resource_list),	

	DEVMETHOD(bus_child_pnpinfo_str, ofw_bus_gen_child_pnpinfo_str),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	macio_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	{ 0, 0 }
};

static driver_t macio_pci_driver = {
        "macio",
        macio_methods,
	sizeof(struct macio_softc)
};

devclass_t macio_devclass;

EARLY_DRIVER_MODULE(macio, pci, macio_pci_driver, macio_devclass, 0, 0,
    BUS_PASS_BUS);

/*
 * PCI ID search table
 */
static struct macio_pci_dev {
        u_int32_t  mpd_devid;
	char    *mpd_desc;
} macio_pci_devlist[] = {
	{ 0x0017106b, "Paddington I/O Controller" },
	{ 0x0022106b, "KeyLargo I/O Controller" },
	{ 0x0025106b, "Pangea I/O Controller" },
	{ 0x003e106b, "Intrepid I/O Controller" },
	{ 0x0041106b, "K2 KeyLargo I/O Controller" },
	{ 0x004f106b, "Shasta I/O Controller" },
	{ 0, NULL }
};

/*
 * Devices to exclude from the probe
 * XXX some of these may be required in the future...
 */
#define	MACIO_QUIRK_IGNORE		0x00000001
#define	MACIO_QUIRK_CHILD_HAS_INTR	0x00000002
#define	MACIO_QUIRK_USE_CHILD_REG	0x00000004

struct macio_quirk_entry {
	const char	*mq_name;
	int		mq_quirks;
};

static struct macio_quirk_entry macio_quirks[] = {
	{ "escc-legacy",		MACIO_QUIRK_IGNORE },
	{ "timer",			MACIO_QUIRK_IGNORE },
	{ "escc",			MACIO_QUIRK_CHILD_HAS_INTR },
        { "i2s", 			MACIO_QUIRK_CHILD_HAS_INTR | 
					MACIO_QUIRK_USE_CHILD_REG },
	{ NULL,				0 }
};

static int
macio_get_quirks(const char *name)
{
        struct	macio_quirk_entry *mqe;

        for (mqe = macio_quirks; mqe->mq_name != NULL; mqe++)
                if (strcmp(name, mqe->mq_name) == 0)
                        return (mqe->mq_quirks);
        return (0);
}


/*
 * Add an interrupt to the dev's resource list if present
 */
static void
macio_add_intr(phandle_t devnode, struct macio_devinfo *dinfo)
{
	phandle_t iparent;
	int	*intr;
	int	i, nintr;
	int 	icells;

	if (dinfo->mdi_ninterrupts >= 6) {
		printf("macio: device has more than 6 interrupts\n");
		return;
	}

	nintr = OF_getprop_alloc_multi(devnode, "interrupts", sizeof(*intr), 
		(void **)&intr);
	if (nintr == -1) {
		nintr = OF_getprop_alloc_multi(devnode, "AAPL,interrupts", 
			sizeof(*intr), (void **)&intr);
		if (nintr == -1)
			return;
	}

	if (intr[0] == -1)
		return;

	if (OF_getprop(devnode, "interrupt-parent", &iparent, sizeof(iparent))
	    <= 0)
		panic("Interrupt but no interrupt parent!\n");

	if (OF_getprop(OF_node_from_xref(iparent), "#interrupt-cells", &icells,
	    sizeof(icells)) <= 0)
		icells = 1;

	for (i = 0; i < nintr; i+=icells) {
		u_int irq = MAP_IRQ(iparent, intr[i]);

		resource_list_add(&dinfo->mdi_resources, SYS_RES_IRQ,
		    dinfo->mdi_ninterrupts, irq, irq, 1);

		dinfo->mdi_interrupts[dinfo->mdi_ninterrupts] = irq;
		dinfo->mdi_ninterrupts++;
	}
}


static void
macio_add_reg(phandle_t devnode, struct macio_devinfo *dinfo)
{
	struct		macio_reg *reg, *regp;
	phandle_t 	child;
	char		buf[8];
	int		i, layout_id = 0, nreg, res;

	nreg = OF_getprop_alloc_multi(devnode, "reg", sizeof(*reg), (void **)&reg);
	if (nreg == -1)
		return;

        /*
         *  Some G5's have broken properties in the i2s-a area. If so we try
         *  to fix it. Right now we know of two different cases, one for
         *  sound layout-id 36 and the other one for sound layout-id 76.
         *  What is missing is the base address for the memory addresses.
         *  We take them from the parent node (i2s) and use the size
         *  information from the child. 
         */

        if (reg[0].mr_base == 0) {
		child = OF_child(devnode);
		while (child != 0) {
			res = OF_getprop(child, "name", buf, sizeof(buf));
			if (res > 0 && strcmp(buf, "sound") == 0)
				break;
			child = OF_peer(child);
		}

                res = OF_getprop(child, "layout-id", &layout_id,
				sizeof(layout_id));

                if (res > 0 && (layout_id == 36 || layout_id == 76)) {
                        res = OF_getprop_alloc_multi(OF_parent(devnode), "reg",
						sizeof(*regp), (void **)&regp);
                        reg[0] = regp[0];
                        reg[1].mr_base = regp[1].mr_base;
                        reg[2].mr_base = regp[1].mr_base + reg[1].mr_size;
                }
        } 

	for (i = 0; i < nreg; i++) {
		resource_list_add(&dinfo->mdi_resources, SYS_RES_MEMORY, i,
		    reg[i].mr_base, reg[i].mr_base + reg[i].mr_size,
		    reg[i].mr_size);
	}
}

/*
 * PCI probe
 */
static int
macio_probe(device_t dev)
{
        int i;
        u_int32_t devid;
	
        devid = pci_get_devid(dev);
        for (i = 0; macio_pci_devlist[i].mpd_desc != NULL; i++) {
                if (devid == macio_pci_devlist[i].mpd_devid) {
                        device_set_desc(dev, macio_pci_devlist[i].mpd_desc);
                        return (0);
                }
        }
	
        return (ENXIO);	
}

/*
 * PCI attach: scan Open Firmware child nodes, and attach these as children
 * of the macio bus
 */
static int 
macio_attach(device_t dev)
{
	struct macio_softc *sc;
        struct macio_devinfo *dinfo;
        phandle_t  root;
	phandle_t  child;
	phandle_t  subchild;
        device_t cdev;
        u_int reg[3];
	char compat[32];
	int error, quirks;

	sc = device_get_softc(dev);
	root = sc->sc_node = ofw_bus_get_node(dev);
	
	/*
	 * Locate the device node and it's base address
	 */
	if (OF_getprop(root, "assigned-addresses", 
		       reg, sizeof(reg)) < (ssize_t)sizeof(reg)) {
		return (ENXIO);
	}

	/* Used later to see if we have to enable the I2S part. */
	OF_getprop(root, "compatible", compat, sizeof(compat));

	sc->sc_base = reg[2];
	sc->sc_size = MACIO_REG_SIZE;

	sc->sc_memrid = PCIR_BAR(0);
	sc->sc_memr = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_memrid, RF_ACTIVE);

	sc->sc_mem_rman.rm_type = RMAN_ARRAY;
	sc->sc_mem_rman.rm_descr = "MacIO Device Memory";
	error = rman_init(&sc->sc_mem_rman);
	if (error) {
		device_printf(dev, "rman_init() failed. error = %d\n", error);
		return (error);
	}
	error = rman_manage_region(&sc->sc_mem_rman, 0, sc->sc_size);	
	if (error) {
		device_printf(dev,
		    "rman_manage_region() failed. error = %d\n", error);
		return (error);
	}

	/*
	 * Iterate through the sub-devices
	 */
	for (child = OF_child(root); child != 0; child = OF_peer(child)) {
		dinfo = malloc(sizeof(*dinfo), M_MACIO, M_WAITOK | M_ZERO);
		if (ofw_bus_gen_setup_devinfo(&dinfo->mdi_obdinfo, child) !=
		    0) {
			free(dinfo, M_MACIO);
			continue;
		}
		quirks = macio_get_quirks(dinfo->mdi_obdinfo.obd_name);
		if ((quirks & MACIO_QUIRK_IGNORE) != 0) {
			ofw_bus_gen_destroy_devinfo(&dinfo->mdi_obdinfo);
			free(dinfo, M_MACIO);
			continue;
		}
		resource_list_init(&dinfo->mdi_resources);
		dinfo->mdi_ninterrupts = 0;
		macio_add_intr(child, dinfo);
		if ((quirks & MACIO_QUIRK_USE_CHILD_REG) != 0)
			macio_add_reg(OF_child(child), dinfo);
		else
			macio_add_reg(child, dinfo);
		if ((quirks & MACIO_QUIRK_CHILD_HAS_INTR) != 0)
			for (subchild = OF_child(child); subchild != 0;
			    subchild = OF_peer(subchild))
				macio_add_intr(subchild, dinfo);
		cdev = device_add_child(dev, NULL, -1);
		if (cdev == NULL) {
			device_printf(dev, "<%s>: device_add_child failed\n",
			    dinfo->mdi_obdinfo.obd_name);
			resource_list_free(&dinfo->mdi_resources);
			ofw_bus_gen_destroy_devinfo(&dinfo->mdi_obdinfo);
			free(dinfo, M_MACIO);
			continue;
		}
		device_set_ivars(cdev, dinfo);

		/* Set FCRs to enable some devices */
		if (sc->sc_memr == NULL)
			continue;

		if (strcmp(ofw_bus_get_name(cdev), "bmac") == 0 ||
		    (ofw_bus_get_compat(cdev) != NULL &&
		    strcmp(ofw_bus_get_compat(cdev), "bmac+") == 0)) {
			uint32_t fcr;

			fcr = bus_read_4(sc->sc_memr, HEATHROW_FCR);

			fcr |= FCR_ENET_ENABLE & ~FCR_ENET_RESET;
			bus_write_4(sc->sc_memr, HEATHROW_FCR, fcr);
			DELAY(50000);
			fcr |= FCR_ENET_RESET;
			bus_write_4(sc->sc_memr, HEATHROW_FCR, fcr);
			DELAY(50000);
			fcr &= ~FCR_ENET_RESET;
			bus_write_4(sc->sc_memr, HEATHROW_FCR, fcr);
			DELAY(50000);
			
			bus_write_4(sc->sc_memr, HEATHROW_FCR, fcr);
		}

		/*
		 * Make sure the I2S0 and the I2S0_CLK are enabled.
		 * On certain G5's they are not.
		 */
		if ((strcmp(ofw_bus_get_name(cdev), "i2s") == 0) &&
		    (strcmp(compat, "K2-Keylargo") == 0)) {

			uint32_t fcr1;

			fcr1 = bus_read_4(sc->sc_memr, KEYLARGO_FCR1);
			fcr1 |= FCR1_I2S0_CLK_ENABLE | FCR1_I2S0_ENABLE;
			bus_write_4(sc->sc_memr, KEYLARGO_FCR1, fcr1);
		}

	}

	return (bus_generic_attach(dev));
}


static int
macio_print_child(device_t dev, device_t child)
{
        struct macio_devinfo *dinfo;
        struct resource_list *rl;
        int retval = 0;

        dinfo = device_get_ivars(child);
        rl = &dinfo->mdi_resources;

        retval += bus_print_child_header(dev, child);

        retval += resource_list_print_type(rl, "mem", SYS_RES_MEMORY, "%#jx");
        retval += resource_list_print_type(rl, "irq", SYS_RES_IRQ, "%jd");

        retval += bus_print_child_footer(dev, child);

        return (retval);
}


static void
macio_probe_nomatch(device_t dev, device_t child)
{
        struct macio_devinfo *dinfo;
        struct resource_list *rl;
	const char *type;

	if (bootverbose) {
		dinfo = device_get_ivars(child);
		rl = &dinfo->mdi_resources;

		if ((type = ofw_bus_get_type(child)) == NULL)
			type = "(unknown)";
		device_printf(dev, "<%s, %s>", type, ofw_bus_get_name(child));
		resource_list_print_type(rl, "mem", SYS_RES_MEMORY, "%#jx");
		resource_list_print_type(rl, "irq", SYS_RES_IRQ, "%jd");
		printf(" (no driver attached)\n");
	}
}


static struct resource *
macio_alloc_resource(device_t bus, device_t child, int type, int *rid,
		     rman_res_t start, rman_res_t end, rman_res_t count,
		     u_int flags)
{
	struct		macio_softc *sc;
	int		needactivate;
	struct		resource *rv;
	struct		rman *rm;
	u_long		adjstart, adjend, adjcount;
	struct		macio_devinfo *dinfo;
	struct		resource_list_entry *rle;

	sc = device_get_softc(bus);
	dinfo = device_get_ivars(child);

	needactivate = flags & RF_ACTIVE;
	flags &= ~RF_ACTIVE;

	switch (type) {
	case SYS_RES_MEMORY:
	case SYS_RES_IOPORT:
		rle = resource_list_find(&dinfo->mdi_resources, SYS_RES_MEMORY,
		    *rid);
		if (rle == NULL) {
			device_printf(bus, "no rle for %s memory %d\n",
			    device_get_nameunit(child), *rid);
			return (NULL);
		}

		if (start < rle->start)
			adjstart = rle->start;
		else if (start > rle->end)
			adjstart = rle->end;
		else
			adjstart = start;

		if (end < rle->start)
			adjend = rle->start;
		else if (end > rle->end)
			adjend = rle->end;
		else
			adjend = end;

		adjcount = adjend - adjstart;

		rm = &sc->sc_mem_rman;
		break;

	case SYS_RES_IRQ:
		/* Check for passthrough from subattachments like macgpio */
		if (device_get_parent(child) != bus)
			return BUS_ALLOC_RESOURCE(device_get_parent(bus), child,
			    type, rid, start, end, count, flags);

		rle = resource_list_find(&dinfo->mdi_resources, SYS_RES_IRQ,
		    *rid);
		if (rle == NULL) {
			if (dinfo->mdi_ninterrupts >= 6) {
				device_printf(bus,
				    "%s has more than 6 interrupts\n",
				    device_get_nameunit(child));
				return (NULL);
			}
			resource_list_add(&dinfo->mdi_resources, SYS_RES_IRQ,
			    dinfo->mdi_ninterrupts, start, start, 1);

			dinfo->mdi_interrupts[dinfo->mdi_ninterrupts] = start;
			dinfo->mdi_ninterrupts++;
		}

		return (resource_list_alloc(&dinfo->mdi_resources, bus, child,
		    type, rid, start, end, count, flags));

	default:
		device_printf(bus, "unknown resource request from %s\n",
			      device_get_nameunit(child));
		return (NULL);
	}

	rv = rman_reserve_resource(rm, adjstart, adjend, adjcount, flags,
	    child);
	if (rv == NULL) {
		device_printf(bus,
		    "failed to reserve resource %#lx - %#lx (%#lx) for %s\n",
		    adjstart, adjend, adjcount, device_get_nameunit(child));
		return (NULL);
	}

	rman_set_rid(rv, *rid);

	if (needactivate) {
		if (bus_activate_resource(child, type, *rid, rv) != 0) {
                        device_printf(bus,
				      "failed to activate resource for %s\n",
				      device_get_nameunit(child));
			rman_release_resource(rv);
			return (NULL);
                }
        }

	return (rv);
}


static int
macio_release_resource(device_t bus, device_t child, int type, int rid,
		       struct resource *res)
{
	if (rman_get_flags(res) & RF_ACTIVE) {
		int error = bus_deactivate_resource(child, type, rid, res);
		if (error)
			return error;
	}

	return (rman_release_resource(res));
}


static int
macio_activate_resource(device_t bus, device_t child, int type, int rid,
			   struct resource *res)
{
	struct macio_softc *sc;
	void    *p;

	sc = device_get_softc(bus);

	if (type == SYS_RES_IRQ)
                return (bus_activate_resource(bus, type, rid, res));

	if ((type == SYS_RES_MEMORY) || (type == SYS_RES_IOPORT)) {
		p = pmap_mapdev((vm_offset_t)rman_get_start(res) + sc->sc_base,
				(vm_size_t)rman_get_size(res));
		if (p == NULL)
			return (ENOMEM);
		rman_set_virtual(res, p);
		rman_set_bustag(res, &bs_le_tag);
		rman_set_bushandle(res, (u_long)p);
	}

	return (rman_activate_resource(res));
}


static int
macio_deactivate_resource(device_t bus, device_t child, int type, int rid,
			  struct resource *res)
{
        /*
         * If this is a memory resource, unmap it.
         */
        if ((type == SYS_RES_MEMORY) || (type == SYS_RES_IOPORT)) {
		u_int32_t psize;

		psize = rman_get_size(res);
		pmap_unmapdev((vm_offset_t)rman_get_virtual(res), psize);
	}

	return (rman_deactivate_resource(res));
}


static struct resource_list *
macio_get_resource_list (device_t dev, device_t child)
{
	struct macio_devinfo *dinfo;

	dinfo = device_get_ivars(child);
	return (&dinfo->mdi_resources);
}

static const struct ofw_bus_devinfo *
macio_get_devinfo(device_t dev, device_t child)
{
	struct macio_devinfo *dinfo;

	dinfo = device_get_ivars(child);
	return (&dinfo->mdi_obdinfo);
}

int
macio_enable_wireless(device_t dev, bool enable)
{
	struct macio_softc *sc = device_get_softc(dev);
	uint32_t x;

	if (enable) {
		x = bus_read_4(sc->sc_memr, KEYLARGO_FCR2);
		x |= 0x4;
		bus_write_4(sc->sc_memr, KEYLARGO_FCR2, x);

		/* Enable card slot. */
		bus_write_1(sc->sc_memr, KEYLARGO_GPIO_BASE + 0x0f, 5);
		DELAY(1000);
		bus_write_1(sc->sc_memr, KEYLARGO_GPIO_BASE + 0x0f, 4);
		DELAY(1000);
		x = bus_read_4(sc->sc_memr, KEYLARGO_FCR2);
		x &= ~0x80000000;

		bus_write_4(sc->sc_memr, KEYLARGO_FCR2, x);
		/* out8(gpio + 0x10, 4); */

		bus_write_1(sc->sc_memr, KEYLARGO_EXTINT_GPIO_REG_BASE + 0x0b, 0);
		bus_write_1(sc->sc_memr, KEYLARGO_EXTINT_GPIO_REG_BASE + 0x0a, 0x28);
		bus_write_1(sc->sc_memr, KEYLARGO_EXTINT_GPIO_REG_BASE + 0x0d, 0x28);
		bus_write_1(sc->sc_memr, KEYLARGO_GPIO_BASE + 0x0d, 0x28);
		bus_write_1(sc->sc_memr, KEYLARGO_GPIO_BASE + 0x0e, 0x28);
		bus_write_4(sc->sc_memr, 0x1c000, 0);

		/* Initialize the card. */
		bus_write_4(sc->sc_memr, 0x1a3e0, 0x41);
		x = bus_read_4(sc->sc_memr, KEYLARGO_FCR2);
		x |= 0x80000000;
		bus_write_4(sc->sc_memr, KEYLARGO_FCR2, x);
	} else {
		x = bus_read_4(sc->sc_memr, KEYLARGO_FCR2);
		x &= ~0x4;
		bus_write_4(sc->sc_memr, KEYLARGO_FCR2, x);
		/* out8(gpio + 0x10, 0); */
	}

	return (0);
}
