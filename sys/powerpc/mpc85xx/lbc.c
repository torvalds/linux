/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2006-2008, Juniper Networks, Inc.
 * Copyright (c) 2008 Semihalf, Rafal Czubak
 * Copyright (c) 2009 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Semihalf
 * under sponsorship from the FreeBSD Foundation.
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

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ktr.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <machine/bus.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <powerpc/mpc85xx/mpc85xx.h>

#include "ofw_bus_if.h"
#include "lbc.h"

#ifdef DEBUG
#define debugf(fmt, args...) do { printf("%s(): ", __func__);	\
    printf(fmt,##args); } while (0)
#else
#define debugf(fmt, args...)
#endif

static MALLOC_DEFINE(M_LBC, "localbus", "localbus devices information");

static int lbc_probe(device_t);
static int lbc_attach(device_t);
static int lbc_shutdown(device_t);
static int lbc_activate_resource(device_t bus __unused, device_t child __unused,
    int type, int rid __unused, struct resource *r);
static int lbc_deactivate_resource(device_t bus __unused,
    device_t child __unused, int type __unused, int rid __unused,
    struct resource *r);
static struct resource *lbc_alloc_resource(device_t, device_t, int, int *,
    rman_res_t, rman_res_t, rman_res_t, u_int);
static int lbc_print_child(device_t, device_t);
static int lbc_release_resource(device_t, device_t, int, int,
    struct resource *);
static const struct ofw_bus_devinfo *lbc_get_devinfo(device_t, device_t);

/*
 * Bus interface definition
 */
static device_method_t lbc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		lbc_probe),
	DEVMETHOD(device_attach,	lbc_attach),
	DEVMETHOD(device_shutdown,	lbc_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_print_child,	lbc_print_child),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	NULL),

	DEVMETHOD(bus_alloc_resource,	lbc_alloc_resource),
	DEVMETHOD(bus_release_resource,	lbc_release_resource),
	DEVMETHOD(bus_activate_resource, lbc_activate_resource),
	DEVMETHOD(bus_deactivate_resource, lbc_deactivate_resource),

	/* OFW bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	lbc_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	{ 0, 0 }
};

static driver_t lbc_driver = {
	"lbc",
	lbc_methods,
	sizeof(struct lbc_softc)
};

devclass_t lbc_devclass;

EARLY_DRIVER_MODULE(lbc, ofwbus, lbc_driver, lbc_devclass,
    0, 0, BUS_PASS_BUS);

/*
 * Calculate address mask used by OR(n) registers. Use memory region size to
 * determine mask value. The size must be a power of two and within the range
 * of 32KB - 4GB. Otherwise error code is returned. Value representing
 * 4GB size can be passed as 0xffffffff.
 */
static uint32_t
lbc_address_mask(uint32_t size)
{
	int n = 15;

	if (size == ~0)
		return (0);

	while (n < 32) {
		if (size == (1U << n))
			break;
		n++;
	}

	if (n == 32)
		return (EINVAL);

	return (0xffff8000 << (n - 15));
}

static void
lbc_banks_unmap(struct lbc_softc *sc)
{
	int r;

	r = 0;
	while (r < LBC_DEV_MAX) {
		if (sc->sc_range[r].size == 0)
			return;

		pmap_unmapdev(sc->sc_range[r].kva, sc->sc_range[r].size);
		law_disable(OCP85XX_TGTIF_LBC, sc->sc_range[r].addr,
		    sc->sc_range[r].size);
		r++;
	}
}

static int
lbc_banks_map(struct lbc_softc *sc)
{
	vm_paddr_t end, start;
	vm_size_t size;
	u_int i, r, ranges, s;
	int error;

	bzero(sc->sc_range, sizeof(sc->sc_range));

	/*
	 * Determine number of discontiguous address ranges to program.
	 */
	ranges = 0;
	for (i = 0; i < LBC_DEV_MAX; i++) {
		size = sc->sc_banks[i].size;
		if (size == 0)
			continue;

		start = sc->sc_banks[i].addr;
		for (r = 0; r < ranges; r++) {
			/* Avoid wrap-around bugs. */
			end = sc->sc_range[r].addr - 1 + sc->sc_range[r].size;
			if (start > 0 && end == start - 1) {
				sc->sc_range[r].size += size;
				break;
			}
			/* Avoid wrap-around bugs. */
			end = start - 1 + size;
			if (sc->sc_range[r].addr > 0 &&
			    end == sc->sc_range[r].addr - 1) {
				sc->sc_range[r].addr = start;
				sc->sc_range[r].size += size;
				break;
			}
		}
		if (r == ranges) {
			/* New range; add using insertion sort */
			r = 0;
			while (r < ranges && sc->sc_range[r].addr < start)
				r++;
			for (s = ranges; s > r; s--)
				sc->sc_range[s] = sc->sc_range[s-1];
			sc->sc_range[r].addr = start;
			sc->sc_range[r].size = size;
			ranges++;
		}
	}

	/*
	 * Ranges are sorted so quickly go over the list to merge ranges
	 * that grew toward each other while building the ranges.
	 */
	r = 0;
	while (r < ranges - 1) {
		end = sc->sc_range[r].addr + sc->sc_range[r].size;
		if (end != sc->sc_range[r+1].addr) {
			r++;
			continue;
		}
		sc->sc_range[r].size += sc->sc_range[r+1].size;
		for (s = r + 1; s < ranges - 1; s++)
			sc->sc_range[s] = sc->sc_range[s+1];
		bzero(&sc->sc_range[s], sizeof(sc->sc_range[s]));
		ranges--;
	}

	/*
	 * Configure LAW for the LBC ranges and map the physical memory
	 * range into KVA.
	 */
	for (r = 0; r < ranges; r++) {
		start = sc->sc_range[r].addr;
		size = sc->sc_range[r].size;
		error = law_enable(OCP85XX_TGTIF_LBC, start, size);
		if (error)
			return (error);
		sc->sc_range[r].kva = (vm_offset_t)pmap_mapdev(start, size);
	}

	/* XXX: need something better here? */
	if (ranges == 0)
		return (EINVAL);

	/* Assign KVA to banks based on the enclosing range. */
	for (i = 0; i < LBC_DEV_MAX; i++) {
		size = sc->sc_banks[i].size;
		if (size == 0)
			continue;

		start = sc->sc_banks[i].addr;
		for (r = 0; r < ranges; r++) {
			end = sc->sc_range[r].addr - 1 + sc->sc_range[r].size;
			if (start >= sc->sc_range[r].addr &&
			    start - 1 + size <= end)
				break;
		}
		if (r < ranges) {
			sc->sc_banks[i].kva = sc->sc_range[r].kva +
			    (start - sc->sc_range[r].addr);
		}
	}

	return (0);
}

static int
lbc_banks_enable(struct lbc_softc *sc)
{
	uint32_t size;
	uint32_t regval;
	int error, i;

	for (i = 0; i < LBC_DEV_MAX; i++) {
		size = sc->sc_banks[i].size;
		if (size == 0)
			continue;

		/*
		 * Compute and program BR value.
		 */
		regval = sc->sc_banks[i].addr;
		switch (sc->sc_banks[i].width) {
		case 8:
			regval |= (1 << 11);
			break;
		case 16:
			regval |= (2 << 11);
			break;
		case 32:
			regval |= (3 << 11);
			break;
		default:
			error = EINVAL;
			goto fail;
		}
		regval |= (sc->sc_banks[i].decc << 9);
		regval |= (sc->sc_banks[i].wp << 8);
		regval |= (sc->sc_banks[i].msel << 5);
		regval |= (sc->sc_banks[i].atom << 2);
		regval |= 1;
		bus_space_write_4(sc->sc_bst, sc->sc_bsh,
		    LBC85XX_BR(i), regval);

		/*
		 * Compute and program OR value.
		 */
		regval = lbc_address_mask(size);
		switch (sc->sc_banks[i].msel) {
		case LBCRES_MSEL_GPCM:
			/* TODO Add flag support for option registers */
			regval |= 0x0ff7;
			break;
		case LBCRES_MSEL_FCM:
			/* TODO Add flag support for options register */
			regval |= 0x0796;
			break;
		case LBCRES_MSEL_UPMA:
		case LBCRES_MSEL_UPMB:
		case LBCRES_MSEL_UPMC:
			printf("UPM mode not supported yet!");
			error = ENOSYS;
			goto fail;
		}
		bus_space_write_4(sc->sc_bst, sc->sc_bsh,
		    LBC85XX_OR(i), regval);
	}

	return (0);

fail:
	lbc_banks_unmap(sc);
	return (error);
}

static void
fdt_lbc_fixup(phandle_t node, struct lbc_softc *sc, struct lbc_devinfo *di)
{
	pcell_t width;
	int bank;

	if (OF_getprop(node, "bank-width", (void *)&width, sizeof(width)) <= 0)
		return;

	bank = di->di_bank;
	if (sc->sc_banks[bank].size == 0)
		return;

	/* Express width in bits. */
	sc->sc_banks[bank].width = width * 8;
}

static int
fdt_lbc_reg_decode(phandle_t node, struct lbc_softc *sc,
    struct lbc_devinfo *di)
{
	rman_res_t start, end, count;
	pcell_t *reg, *regptr;
	pcell_t addr_cells, size_cells;
	int tuple_size, tuples;
	int i, j, rv, bank;

	if (fdt_addrsize_cells(OF_parent(node), &addr_cells, &size_cells) != 0)
		return (ENXIO);

	tuple_size = sizeof(pcell_t) * (addr_cells + size_cells);
	tuples = OF_getencprop_alloc_multi(node, "reg", tuple_size,
	    (void **)&reg);
	debugf("addr_cells = %d, size_cells = %d\n", addr_cells, size_cells);
	debugf("tuples = %d, tuple size = %d\n", tuples, tuple_size);
	if (tuples <= 0)
		/* No 'reg' property in this node. */
		return (0);

	regptr = reg;
	for (i = 0; i < tuples; i++) {

		bank = fdt_data_get((void *)reg, 1);
		di->di_bank = bank;
		reg += 1;

		/* Get address/size. */
		start = count = 0;
		for (j = 0; j < addr_cells; j++) {
			start <<= 32;
			start |= reg[j];
		}
		for (j = 0; j < size_cells; j++) {
			count <<= 32;
			count |= reg[addr_cells + j - 1];
		}
		reg += addr_cells - 1 + size_cells;

		/* Calculate address range relative to VA base. */
		start = sc->sc_banks[bank].kva + start;
		end = start + count - 1;

		debugf("reg addr bank = %d, start = %jx, end = %jx, "
		    "count = %jx\n", bank, start, end, count);

		/* Use bank (CS) cell as rid. */
		resource_list_add(&di->di_res, SYS_RES_MEMORY, bank, start,
		    end, count);
	}
	rv = 0;
	OF_prop_free(regptr);
	return (rv);
}

static void
lbc_intr(void *arg)
{
	struct lbc_softc *sc = arg;
	uint32_t ltesr;

	ltesr = bus_space_read_4(sc->sc_bst, sc->sc_bsh, LBC85XX_LTESR);
	sc->sc_ltesr = ltesr;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, LBC85XX_LTESR, ltesr);
	wakeup(sc->sc_dev);
}

static int
lbc_probe(device_t dev)
{

	if (!(ofw_bus_is_compatible(dev, "fsl,lbc") ||
	    ofw_bus_is_compatible(dev, "fsl,elbc")))
		return (ENXIO);

	device_set_desc(dev, "Freescale Local Bus Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
lbc_attach(device_t dev)
{
	struct lbc_softc *sc;
	struct lbc_devinfo *di;
	struct rman *rm;
	uintmax_t offset, size;
	vm_paddr_t start;
	device_t cdev;
	phandle_t node, child;
	pcell_t *ranges, *rangesptr;
	int tuple_size, tuples;
	int par_addr_cells;
	int bank, error, i, j;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	sc->sc_mrid = 0;
	sc->sc_mres = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->sc_mrid,
	    RF_ACTIVE);
	if (sc->sc_mres == NULL)
		return (ENXIO);

	sc->sc_bst = rman_get_bustag(sc->sc_mres);
	sc->sc_bsh = rman_get_bushandle(sc->sc_mres);

	for (bank = 0; bank < LBC_DEV_MAX; bank++) {
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, LBC85XX_BR(bank), 0);
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, LBC85XX_OR(bank), 0);
	}

	/*
	 * Initialize configuration register:
	 * - enable Local Bus
	 * - set data buffer control signal function
	 * - disable parity byte select
	 * - set ECC parity type
	 * - set bus monitor timing and timer prescale
	 */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, LBC85XX_LBCR, 0);

	/*
	 * Initialize clock ratio register:
	 * - disable PLL bypass mode
	 * - configure LCLK delay cycles for the assertion of LALE
	 * - set system clock divider
	 */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, LBC85XX_LCRR, 0x00030008);

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, LBC85XX_LTEDR, 0);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, LBC85XX_LTESR, ~0);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, LBC85XX_LTEIR, 0x64080001);

	sc->sc_irid = 0;
	sc->sc_ires = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->sc_irid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->sc_ires != NULL) {
		error = bus_setup_intr(dev, sc->sc_ires,
		    INTR_TYPE_MISC | INTR_MPSAFE, NULL, lbc_intr, sc,
		    &sc->sc_icookie);
		if (error) {
			device_printf(dev, "could not activate interrupt\n");
			bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irid,
			    sc->sc_ires);
			sc->sc_ires = NULL;
		}
	}

	sc->sc_ltesr = ~0;

	rangesptr = NULL;

	rm = &sc->sc_rman;
	rm->rm_type = RMAN_ARRAY;
	rm->rm_descr = "Local Bus Space";
	error = rman_init(rm);
	if (error)
		goto fail;

	error = rman_manage_region(rm, rm->rm_start, rm->rm_end);
	if (error) {
		rman_fini(rm);
		goto fail;
	}

	/*
	 * Process 'ranges' property.
	 */
	node = ofw_bus_get_node(dev);
	if ((fdt_addrsize_cells(node, &sc->sc_addr_cells,
	    &sc->sc_size_cells)) != 0) {
		error = ENXIO;
		goto fail;
	}

	par_addr_cells = fdt_parent_addr_cells(node);
	if (par_addr_cells > 2) {
		device_printf(dev, "unsupported parent #addr-cells\n");
		error = ERANGE;
		goto fail;
	}
	tuple_size = sizeof(pcell_t) * (sc->sc_addr_cells + par_addr_cells +
	    sc->sc_size_cells);

	tuples = OF_getencprop_alloc_multi(node, "ranges", tuple_size,
	    (void **)&ranges);
	if (tuples < 0) {
		device_printf(dev, "could not retrieve 'ranges' property\n");
		error = ENXIO;
		goto fail;
	}
	rangesptr = ranges;

	debugf("par addr_cells = %d, addr_cells = %d, size_cells = %d, "
	    "tuple_size = %d, tuples = %d\n", par_addr_cells,
	    sc->sc_addr_cells, sc->sc_size_cells, tuple_size, tuples);

	start = 0;
	size = 0;
	for (i = 0; i < tuples; i++) {

		/* The first cell is the bank (chip select) number. */
		bank = fdt_data_get(ranges, 1);
		if (bank < 0 || bank > LBC_DEV_MAX) {
			device_printf(dev, "bank out of range: %d\n", bank);
			error = ERANGE;
			goto fail;
		}
		ranges += 1;

		/*
		 * Remaining cells of the child address define offset into
		 * this CS.
		 */
		offset = 0;
		for (j = 0; j < sc->sc_addr_cells - 1; j++) {
			offset <<= sizeof(pcell_t) * 8;
			offset |= *ranges;
			ranges++;
		}

		/* Parent bus start address of this bank. */
		start = 0;
		for (j = 0; j < par_addr_cells; j++) {
			start <<= sizeof(pcell_t) * 8;
			start |= *ranges;
			ranges++;
		}

		size = fdt_data_get((void *)ranges, sc->sc_size_cells);
		ranges += sc->sc_size_cells;
		debugf("bank = %d, start = %jx, size = %jx\n", bank,
		    (uintmax_t)start, size);

		sc->sc_banks[bank].addr = start + offset;
		sc->sc_banks[bank].size = size;

		/*
		 * Attributes for the bank.
		 *
		 * XXX Note there are no DT bindings defined for them at the
		 * moment, so we need to provide some defaults.
		 */
		sc->sc_banks[bank].width = 16;
		sc->sc_banks[bank].msel = LBCRES_MSEL_GPCM;
		sc->sc_banks[bank].decc = LBCRES_DECC_DISABLED;
		sc->sc_banks[bank].atom = LBCRES_ATOM_DISABLED;
		sc->sc_banks[bank].wp = 0;
	}

	/*
	 * Initialize mem-mappings for the LBC banks (i.e. chip selects).
	 */
	error = lbc_banks_map(sc);
	if (error)
		goto fail;

	/*
	 * Walk the localbus and add direct subordinates as our children.
	 */
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {

		di = malloc(sizeof(*di), M_LBC, M_WAITOK | M_ZERO);

		if (ofw_bus_gen_setup_devinfo(&di->di_ofw, child) != 0) {
			free(di, M_LBC);
			device_printf(dev, "could not set up devinfo\n");
			continue;
		}

		resource_list_init(&di->di_res);

		if (fdt_lbc_reg_decode(child, sc, di)) {
			device_printf(dev, "could not process 'reg' "
			    "property\n");
			ofw_bus_gen_destroy_devinfo(&di->di_ofw);
			free(di, M_LBC);
			continue;
		}

		fdt_lbc_fixup(child, sc, di);

		/* Add newbus device for this FDT node */
		cdev = device_add_child(dev, NULL, -1);
		if (cdev == NULL) {
			device_printf(dev, "could not add child: %s\n",
			    di->di_ofw.obd_name);
			resource_list_free(&di->di_res);
			ofw_bus_gen_destroy_devinfo(&di->di_ofw);
			free(di, M_LBC);
			continue;
		}
		debugf("added child name='%s', node=%x\n", di->di_ofw.obd_name,
		    child);
		device_set_ivars(cdev, di);
	}

	/*
	 * Enable the LBC.
	 */
	lbc_banks_enable(sc);

	OF_prop_free(rangesptr);
	return (bus_generic_attach(dev));

fail:
	OF_prop_free(rangesptr);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_mrid, sc->sc_mres);
	return (error);
}

static int
lbc_shutdown(device_t dev)
{

	/* TODO */
	return(0);
}

static struct resource *
lbc_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct lbc_softc *sc;
	struct lbc_devinfo *di;
	struct resource_list_entry *rle;
	struct resource *res;
	struct rman *rm;
	int needactivate;

	/* We only support default allocations. */
	if (!RMAN_IS_DEFAULT_RANGE(start, end))
		return (NULL);

	sc = device_get_softc(bus);
	if (type == SYS_RES_IRQ)
		return (bus_alloc_resource(bus, type, rid, start, end, count,
		    flags));

	/*
	 * Request for the default allocation with a given rid: use resource
	 * list stored in the local device info.
	 */
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
	count = rle->count;
	end = start + count - 1;

	sc = device_get_softc(bus);

	needactivate = flags & RF_ACTIVE;
	flags &= ~RF_ACTIVE;

	rm = &sc->sc_rman;

	res = rman_reserve_resource(rm, start, end, count, flags, child);
	if (res == NULL) {
		device_printf(bus, "failed to reserve resource %#jx - %#jx "
		    "(%#jx)\n", start, end, count);
		return (NULL);
	}

	rman_set_rid(res, *rid);
	rman_set_bustag(res, &bs_be_tag);
	rman_set_bushandle(res, rman_get_start(res));

	if (needactivate)
		if (bus_activate_resource(child, type, *rid, res)) {
			device_printf(child, "resource activation failed\n");
			rman_release_resource(res);
			return (NULL);
		}

	return (res);
}

static int
lbc_print_child(device_t dev, device_t child)
{
	struct lbc_devinfo *di;
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

static int
lbc_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *res)
{
	int err;

	if (rman_get_flags(res) & RF_ACTIVE) {
		err = bus_deactivate_resource(child, type, rid, res);
		if (err)
			return (err);
	}

	return (rman_release_resource(res));
}

static int
lbc_activate_resource(device_t bus __unused, device_t child __unused,
    int type __unused, int rid __unused, struct resource *r)
{

	/* Child resources were already mapped, just activate. */
	return (rman_activate_resource(r));
}

static int
lbc_deactivate_resource(device_t bus __unused, device_t child __unused,
    int type __unused, int rid __unused, struct resource *r)
{

	return (rman_deactivate_resource(r));
}

static const struct ofw_bus_devinfo *
lbc_get_devinfo(device_t bus, device_t child)
{
	struct lbc_devinfo *di;

	di = device_get_ivars(child);
	return (&di->di_ofw);
}

void
lbc_write_reg(device_t child, u_int off, uint32_t val)
{
	device_t dev;
	struct lbc_softc *sc;

	dev = device_get_parent(child);

	if (off >= 0x1000) {
		device_printf(dev, "%s(%s): invalid offset %#x\n",
		    __func__, device_get_nameunit(child), off);
		return;
	}

	sc = device_get_softc(dev);

	if (off == LBC85XX_LTESR && sc->sc_ltesr != ~0u) {
		sc->sc_ltesr ^= (val & sc->sc_ltesr);
		return;
	}

	if (off == LBC85XX_LTEATR && (val & 1) == 0)
		sc->sc_ltesr = ~0u;
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, off, val);
}

uint32_t
lbc_read_reg(device_t child, u_int off)
{
	device_t dev;
	struct lbc_softc *sc;
	uint32_t val;

	dev = device_get_parent(child);

	if (off >= 0x1000) {
		device_printf(dev, "%s(%s): invalid offset %#x\n",
		    __func__, device_get_nameunit(child), off);
		return (~0U);
	}

	sc = device_get_softc(dev);

	if (off == LBC85XX_LTESR && sc->sc_ltesr != ~0U)
		val = sc->sc_ltesr;
	else
		val = bus_space_read_4(sc->sc_bst, sc->sc_bsh, off);
	return (val);
}
