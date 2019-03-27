/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Semihalf.
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

#include "opt_platform.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ktr.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/malloc.h>
#include <sys/devmap.h>

#include <vm/vm.h>

#include <machine/fdt.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include "dev/fdt/fdt_common.h"
#include "ofw_bus_if.h"

#include <arm/mv/mvvar.h>
#include <arm/mv/mvwin.h>

#ifdef DEBUG
#define debugf(fmt, args...) do { printf("%s(): ", __func__);	\
    printf(fmt,##args); } while (0)
#else
#define debugf(fmt, args...)
#endif

#define MV_LOCALBUS_MAX_BANKS		8
#define MV_LOCALBUS_MAX_BANK_CELLS	4

static MALLOC_DEFINE(M_LOCALBUS, "localbus", "localbus devices information");

struct localbus_bank {
	vm_offset_t	va;		/* VA of the bank */
	vm_paddr_t	pa;		/* physical address of the bank */
	vm_size_t	size;		/* bank size */
	uint8_t		mapped;		/* device memory has mapping */
};

struct localbus_softc {
	device_t		sc_dev;
	bus_space_handle_t	sc_bsh;
	bus_space_tag_t		sc_bst;
	int			sc_rid;

	struct localbus_bank	*sc_banks;
};

struct localbus_devinfo {
	struct ofw_bus_devinfo	di_ofw;
	struct resource_list	di_res;
	int			di_bank;
};

struct localbus_va_entry {
	int8_t		bank;
	vm_offset_t 	va;
	vm_size_t 	size;
};

/*
 * Prototypes.
 */
static int localbus_probe(device_t);
static int localbus_attach(device_t);
static int localbus_print_child(device_t, device_t);

static struct resource *localbus_alloc_resource(device_t, device_t, int,
    int *, rman_res_t, rman_res_t, rman_res_t, u_int);
static struct resource_list *localbus_get_resource_list(device_t, device_t);

static ofw_bus_get_devinfo_t localbus_get_devinfo;

/*
 * Bus interface definition.
 */
static device_method_t localbus_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		localbus_probe),
	DEVMETHOD(device_attach,	localbus_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	localbus_print_child),
	DEVMETHOD(bus_alloc_resource,	localbus_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
	DEVMETHOD(bus_get_resource_list, localbus_get_resource_list),

	/* OFW bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	localbus_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	{ 0, 0 }
};

static driver_t localbus_driver = {
	"localbus",
	localbus_methods,
	sizeof(struct localbus_softc)
};

const struct localbus_va_entry localbus_virtmap[] = {
	{  0, MV_DEV_BOOT_BASE,		MV_DEV_BOOT_SIZE },
	{  1, MV_DEV_CS0_BASE,		MV_DEV_CS0_SIZE },
	{  2, MV_DEV_CS1_BASE,		MV_DEV_CS1_SIZE },
	{  3, MV_DEV_CS2_BASE,		MV_DEV_CS2_SIZE },

	{ -1, 0, 0 }
};

static struct localbus_bank localbus_banks[MV_LOCALBUS_MAX_BANKS];

devclass_t localbus_devclass;

DRIVER_MODULE(localbus, ofwbus, localbus_driver, localbus_devclass, 0, 0);

static int
fdt_localbus_reg_decode(phandle_t node, struct localbus_softc *sc,
    struct localbus_devinfo *di)
{
	u_long start, end, count;
	pcell_t *reg, *regptr;
	pcell_t addr_cells, size_cells;
	int tuple_size, tuples;
	int i, rv, bank;

	if (fdt_addrsize_cells(OF_parent(node), &addr_cells, &size_cells) != 0)
		return (ENXIO);

	tuple_size = sizeof(pcell_t) * (addr_cells + size_cells);
	tuples = OF_getprop_alloc_multi(node, "reg", tuple_size, (void **)&reg);
	debugf("addr_cells = %d, size_cells = %d\n", addr_cells, size_cells);
	debugf("tuples = %d, tuple size = %d\n", tuples, tuple_size);
	if (tuples <= 0)
		/* No 'reg' property in this node. */
		return (0);

	regptr = reg;
	for (i = 0; i < tuples; i++) {

		bank = fdt_data_get((void *)regptr, 1);

		if (bank >= MV_LOCALBUS_MAX_BANKS) {
			device_printf(sc->sc_dev, "bank number [%d] out of "
			    "range\n", bank);
			continue;
		}

		/*
		 * If device doesn't have virtual to physical mapping don't add
		 * resources
		 */
		if (!(sc->sc_banks[bank].mapped)) {
			device_printf(sc->sc_dev, "device [%d]: missing memory "
			    "mapping\n", bank);
			continue;
		}

		di->di_bank = bank;
		regptr += 1;

		/* Get address/size. */
		rv = fdt_data_to_res(regptr, addr_cells - 1, size_cells, &start,
		    &count);
		if (rv != 0) {
			resource_list_free(&di->di_res);
			goto out;
		}

		/* Check if enough amount of memory is mapped */
		if (sc->sc_banks[bank].size < count) {
			device_printf(sc->sc_dev, "device [%d]: not enough "
			    "memory reserved\n", bank);
			continue;
		}

		regptr += addr_cells - 1 + size_cells;

		/* Calculate address range relative to VA base. */
		start = sc->sc_banks[bank].va + start;
		end = start + count - 1;

		debugf("reg addr bank = %d, start = %lx, end = %lx, "
		    "count = %lx\n", bank, start, end, count);

		/* Use bank (CS) cell as rid. */
		resource_list_add(&di->di_res, SYS_RES_MEMORY, di->di_bank,
		    start, end, count);
	}
	rv = 0;
out:
	OF_prop_free(reg);
	return (rv);
}

static int
localbus_probe(device_t dev)
{

	if (!ofw_bus_is_compatible_strict(dev, "mrvl,lbc"))
		return (ENXIO);

	device_set_desc(dev, "Marvell device bus");

	return (BUS_PROBE_DEFAULT);
}

static int
localbus_attach(device_t dev)
{
	device_t dev_child;
	struct localbus_softc *sc;
	struct localbus_devinfo *di;
	phandle_t dt_node, dt_child;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_banks = localbus_banks;

	/*
	 * Walk localbus and add direct subordinates as our children.
	 */
	dt_node = ofw_bus_get_node(dev);
	for (dt_child = OF_child(dt_node); dt_child != 0;
	    dt_child = OF_peer(dt_child)) {

		/* Check and process 'status' property. */
		if (!(ofw_bus_node_status_okay(dt_child)))
			continue;

		if (!(mv_fdt_pm(dt_child)))
			continue;

		di = malloc(sizeof(*di), M_LOCALBUS, M_WAITOK | M_ZERO);
		if (ofw_bus_gen_setup_devinfo(&di->di_ofw, dt_child) != 0) {
			free(di, M_LOCALBUS);
			device_printf(dev, "could not set up devinfo\n");
			continue;
		}

		resource_list_init(&di->di_res);
		if (fdt_localbus_reg_decode(dt_child, sc, di)) {
			device_printf(dev, "could not process 'reg' "
			    "property\n");
			ofw_bus_gen_destroy_devinfo(&di->di_ofw);
			free(di, M_LOCALBUS);
			continue;
		}

		/* Add newbus device for this FDT node */
		dev_child = device_add_child(dev, NULL, -1);
		if (dev_child == NULL) {
			device_printf(dev, "could not add child: %s\n",
			    di->di_ofw.obd_name);
			resource_list_free(&di->di_res);
			ofw_bus_gen_destroy_devinfo(&di->di_ofw);
			free(di, M_LOCALBUS);
			continue;
		}
#ifdef DEBUG
		device_printf(dev, "added child: %s\n\n", di->di_ofw.obd_name);
#endif
		device_set_ivars(dev_child, di);
	}

	return (bus_generic_attach(dev));
}

static int
localbus_print_child(device_t dev, device_t child)
{
	struct localbus_devinfo *di;
	struct resource_list *rl;
	int rv;

	di = device_get_ivars(child);
	rl = &di->di_res;

	rv = 0;
	rv += bus_print_child_header(dev, child);
	rv += resource_list_print_type(rl, "mem", SYS_RES_MEMORY, "%#jx");
	rv += resource_list_print_type(rl, "irq", SYS_RES_IRQ, "%jd");
	rv += bus_print_child_footer(dev, child);

	return (rv);
}

static struct resource *
localbus_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct localbus_devinfo *di;
	struct resource_list_entry *rle;

	/*
	 * Request for the default allocation with a given rid: use resource
	 * list stored in the local device info.
	 */
	if (RMAN_IS_DEFAULT_RANGE(start, end)) {
		if ((di = device_get_ivars(child)) == NULL)
			return (NULL);

		if (type == SYS_RES_IOPORT)
			type = SYS_RES_MEMORY;

		rid = &di->di_bank;
		rle = resource_list_find(&di->di_res, type, *rid);
		if (rle == NULL) {
			device_printf(bus, "no default resources for "
			    "rid = %d, type = %d\n", *rid, type);
			return (NULL);
		}
		start = rle->start;
		end = rle->end;
		count = rle->count;
	}

	return (bus_generic_alloc_resource(bus, child, type, rid, start, end,
	    count, flags));
}


static struct resource_list *
localbus_get_resource_list(device_t bus, device_t child)
{
	struct localbus_devinfo *di;

	di = device_get_ivars(child);
	return (&di->di_res);
}

static const struct ofw_bus_devinfo *
localbus_get_devinfo(device_t bus, device_t child)
{
	struct localbus_devinfo *di;

	di = device_get_ivars(child);
	return (&di->di_ofw);
}

int
fdt_localbus_devmap(phandle_t dt_node, struct devmap_entry *fdt_devmap,
    int banks_max_num, int *banks_added)
{
	pcell_t ranges[MV_LOCALBUS_MAX_BANKS * MV_LOCALBUS_MAX_BANK_CELLS];
	pcell_t *rangesptr;
	uint32_t tuple_size, bank;
	vm_paddr_t offset;
	vm_size_t size;
	int dev_num, addr_cells, size_cells, par_addr_cells, va_index, i, j, k;

	if ((fdt_addrsize_cells(dt_node, &addr_cells, &size_cells)) != 0)
		return (EINVAL);

	par_addr_cells = fdt_parent_addr_cells(dt_node);
	if (par_addr_cells > 2) {
		/*
		 * Localbus devmap initialization error: unsupported parent
		 * #addr-cells
		 */
		return (ERANGE);
	}

	tuple_size = (addr_cells + par_addr_cells + size_cells);
	if (tuple_size > MV_LOCALBUS_MAX_BANK_CELLS)
		return (ERANGE);

	tuple_size *= sizeof(pcell_t);

	dev_num = OF_getprop(dt_node, "ranges", ranges, sizeof(ranges));
 	if (dev_num <= 0)
		return (EINVAL);

 	/* Calculate number of devices attached to bus */
 	dev_num = dev_num / tuple_size;

 	/*
 	 * If number of ranges > max number of localbus devices,
 	 * additional entries will not be processed
 	 */
 	dev_num = MIN(dev_num, banks_max_num);

 	rangesptr = &ranges[0];
 	j = 0;

 	/* Process data from FDT */
	for (i = 0; i < dev_num; i++) {

		/* First field is bank number */
		bank = fdt_data_get((void *)rangesptr, 1);
		rangesptr += 1;

		if (bank > MV_LOCALBUS_MAX_BANKS) {
			/* Bank out of range */
			rangesptr += ((addr_cells - 1) + par_addr_cells +
			    size_cells);
			continue;
		}

		/* Find virtmap entry for this bank */
		va_index = -1;
		for (k = 0; localbus_virtmap[k].bank >= 0; k++) {
			if (localbus_virtmap[k].bank == bank) {
				va_index = k;
				break;
			}
		}

		/* Check if virtmap entry was found */
		if (va_index == -1) {
			rangesptr += ((addr_cells - 1) + par_addr_cells +
			    size_cells);
			continue;
		}

		/* Remaining child's address fields are unused */
		rangesptr += (addr_cells - 1);

		/* Parent address offset */
		offset = fdt_data_get((void *)rangesptr, par_addr_cells);
		rangesptr += par_addr_cells;

		/* Last field is size */
		size = fdt_data_get((void *)rangesptr, size_cells);
		rangesptr += size_cells;

		if (size > localbus_virtmap[va_index].size) {
			/* Not enough space reserved in virtual memory map */
			continue;
		}

		fdt_devmap[j].pd_va = localbus_virtmap[va_index].va;
		fdt_devmap[j].pd_pa = offset;
		fdt_devmap[j].pd_size = size;

		/* Copy data to structure used by localbus driver */
		localbus_banks[bank].va = fdt_devmap[j].pd_va;
		localbus_banks[bank].pa = fdt_devmap[j].pd_pa;
		localbus_banks[bank].size = fdt_devmap[j].pd_size;
		localbus_banks[bank].mapped = 1;

		j++;
	}

	*banks_added = j;
	return (0);
}
