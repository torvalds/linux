/*-
 * Copyright (c) 2015,2016 Annapurna Labs Ltd. and affiliates
 * All rights reserved.
 *
 * Developed by Semihalf.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/vmem.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "msi_if.h"
#include "pic_if.h"

#define	AL_SPI_INTR		0
#define	AL_EDGE_HIGH		1
#define	ERR_NOT_IN_MAP		-1
#define	IRQ_OFFSET		1
#define	GIC_INTR_CELL_CNT	3
#define	INTR_RANGE_COUNT	2
#define	MAX_MSIX_COUNT		160

static int al_msix_attach(device_t);
static int al_msix_probe(device_t);

static msi_alloc_msi_t al_msix_alloc_msi;
static msi_release_msi_t al_msix_release_msi;
static msi_alloc_msix_t al_msix_alloc_msix;
static msi_release_msix_t al_msix_release_msix;
static msi_map_msi_t al_msix_map_msi;

static int al_find_intr_pos_in_map(device_t, struct intr_irqsrc *);

static struct ofw_compat_data compat_data[] = {
	{"annapurna-labs,al-msix",	true},
	{"annapurna-labs,alpine-msix",	true},
	{NULL,				false}
};

/*
 * Bus interface definitions.
 */
static device_method_t al_msix_methods[] = {
	DEVMETHOD(device_probe,		al_msix_probe),
	DEVMETHOD(device_attach,	al_msix_attach),

	/* Interrupt controller interface */
	DEVMETHOD(msi_alloc_msi,	al_msix_alloc_msi),
	DEVMETHOD(msi_release_msi,	al_msix_release_msi),
	DEVMETHOD(msi_alloc_msix,	al_msix_alloc_msix),
	DEVMETHOD(msi_release_msix,	al_msix_release_msix),
	DEVMETHOD(msi_map_msi,		al_msix_map_msi),

	DEVMETHOD_END
};

struct al_msix_softc {
	bus_addr_t	base_addr;
	struct resource	*res;
	uint32_t	irq_min;
	uint32_t	irq_max;
	uint32_t	irq_count;
	struct mtx	msi_mtx;
	vmem_t		*irq_alloc;
	device_t	gic_dev;
	/* Table of isrcs maps isrc pointer to vmem_alloc'd irq number */
	struct intr_irqsrc	*isrcs[MAX_MSIX_COUNT];
};

static driver_t al_msix_driver = {
	"al_msix",
	al_msix_methods,
	sizeof(struct al_msix_softc),
};

devclass_t al_msix_devclass;

DRIVER_MODULE(al_msix, ofwbus, al_msix_driver, al_msix_devclass, 0, 0);
DRIVER_MODULE(al_msix, simplebus, al_msix_driver, al_msix_devclass, 0, 0);

MALLOC_DECLARE(M_AL_MSIX);
MALLOC_DEFINE(M_AL_MSIX, "al_msix", "Alpine MSIX");

static int
al_msix_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Annapurna-Labs MSI-X Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
al_msix_attach(device_t dev)
{
	struct al_msix_softc	*sc;
	device_t		gic_dev;
	phandle_t		iparent;
	phandle_t		node;
	intptr_t		xref;
	int			interrupts[INTR_RANGE_COUNT];
	int			nintr, i, rid;
	uint32_t		icells, *intr;

	sc = device_get_softc(dev);

	node = ofw_bus_get_node(dev);
	xref = OF_xref_from_node(node);
	OF_device_register_xref(xref, dev);

	rid = 0;
	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->res == NULL) {
		device_printf(dev, "Failed to allocate resource\n");
		return (ENXIO);
	}

	sc->base_addr = (bus_addr_t)rman_get_start(sc->res);

	/* Register this device to handle MSI interrupts */
	if (intr_msi_register(dev, xref) != 0) {
		device_printf(dev, "could not register MSI-X controller\n");
		return (ENXIO);
	}
	else
		device_printf(dev, "MSI-X controller registered\n");

	/* Find root interrupt controller */
	iparent = ofw_bus_find_iparent(node);
	if (iparent == 0) {
		device_printf(dev, "No interrupt-parrent found. "
				"Error in DTB\n");
		return (ENXIO);
	} else {
		/* While at parent - store interrupt cells prop */
		if (OF_searchencprop(OF_node_from_xref(iparent),
		    "#interrupt-cells", &icells, sizeof(icells)) == -1) {
			device_printf(dev, "DTB: Missing #interrupt-cells "
			    "property in GIC node\n");
			return (ENXIO);
		}
	}

	gic_dev = OF_device_from_xref(iparent);
	if (gic_dev == NULL) {
		device_printf(dev, "Cannot find GIC device\n");
		return (ENXIO);
	}
	sc->gic_dev = gic_dev;

	/* Manually read range of interrupts from DTB */
	nintr = OF_getencprop_alloc_multi(node, "interrupts", sizeof(*intr),
	    (void **)&intr);
	if (nintr == 0) {
		device_printf(dev, "Cannot read interrupts prop from DTB\n");
		return (ENXIO);
	} else if ((nintr / icells) != INTR_RANGE_COUNT) {
		/* Supposed to have min and max value only */
		device_printf(dev, "Unexpected count of interrupts "
				"in DTB node\n");
		return (EINVAL);
	}

	/* Read interrupt range values */
	for (i = 0; i < INTR_RANGE_COUNT; i++)
		interrupts[i] = intr[(i * icells) + IRQ_OFFSET];

	sc->irq_min = interrupts[0];
	sc->irq_max = interrupts[1];
	sc->irq_count = (sc->irq_max - sc->irq_min + 1);

	if (sc->irq_count > MAX_MSIX_COUNT) {
		device_printf(dev, "Available MSI-X count exceeds buffer size."
				" Capping to %d\n", MAX_MSIX_COUNT);
		sc->irq_count = MAX_MSIX_COUNT;
	}

	mtx_init(&sc->msi_mtx, "msi_mtx", NULL, MTX_DEF);

	sc->irq_alloc = vmem_create("Alpine MSI-X IRQs", 0, sc->irq_count,
	    1, 0, M_FIRSTFIT | M_WAITOK);

	device_printf(dev, "MSI-X SPI IRQ %d-%d\n", sc->irq_min, sc->irq_max);

	return (bus_generic_attach(dev));
}

static int
al_find_intr_pos_in_map(device_t dev, struct intr_irqsrc *isrc)
{
	struct al_msix_softc *sc;
	int i;

	sc = device_get_softc(dev);
	for (i = 0; i < MAX_MSIX_COUNT; i++)
		if (sc->isrcs[i] == isrc)
			return (i);
	return (ERR_NOT_IN_MAP);
}

static int
al_msix_map_msi(device_t dev, device_t child, struct intr_irqsrc *isrc,
    uint64_t *addr, uint32_t *data)
{
	struct al_msix_softc *sc;
	int i, spi;

	sc = device_get_softc(dev);

	i = al_find_intr_pos_in_map(dev, isrc);
	if (i == ERR_NOT_IN_MAP)
		return (EINVAL);

	spi = sc->irq_min + i;

	/*
	 * MSIX message address format:
	 * [63:20] - MSIx TBAR
	 *           Same value as the MSIx Translation Base  Address Register
	 * [19]    - WFE_EXIT
	 *           Once set by MSIx message, an EVENTI is signal to the CPUs
	 *           cluster specified by ‘Local GIC Target List’
	 * [18:17] - Target GIC ID
	 *           Specifies which IO-GIC (external shared GIC) is targeted
	 *           0: Local GIC, as specified by the Local GIC Target List
	 *           1: IO-GIC 0
	 *           2: Reserved
	 *           3: Reserved
	 * [16:13] - Local GIC Target List
	 *           Specifies the Local GICs list targeted by this MSIx
	 *           message.
	 *           [16]  If set, SPIn is set in Cluster 0 local GIC
	 *           [15:13] Reserved
	 *           [15]  If set, SPIn is set in Cluster 1 local GIC
	 *           [14]  If set, SPIn is set in Cluster 2 local GIC
	 *           [13]  If set, SPIn is set in Cluster 3 local GIC
	 * [12:3]  - SPIn
	 *           Specifies the SPI (Shared Peripheral Interrupt) index to
	 *           be set in target GICs
	 *           Notes:
	 *           If targeting any local GIC than only SPI[249:0] are valid
	 * [2]     - Function vector
	 *           MSI Data vector extension hint
	 * [1:0]   - Reserved
	 *           Must be set to zero
	 */
	*addr = (uint64_t)sc->base_addr + (uint64_t)((1 << 16) + (spi << 3));
	*data = 0;

	if (bootverbose)
		device_printf(dev, "MSI mapping: SPI: %d addr: %jx data: %x\n",
		    spi, (uintmax_t)*addr, *data);
	return (0);
}

static int
al_msix_alloc_msi(device_t dev, device_t child, int count, int maxcount,
    device_t *pic, struct intr_irqsrc **srcs)
{
	struct intr_map_data_fdt *fdt_data;
	struct al_msix_softc *sc;
	vmem_addr_t irq_base;
	int error;
	u_int i, j;

	sc = device_get_softc(dev);

	if ((powerof2(count) == 0) || (count > 8))
		return (EINVAL);

	if (vmem_alloc(sc->irq_alloc, count, M_FIRSTFIT | M_NOWAIT,
	    &irq_base) != 0)
		return (ENOMEM);

	/* Fabricate OFW data to get ISRC from GIC and return it */
	fdt_data = malloc(sizeof(*fdt_data) +
	    GIC_INTR_CELL_CNT * sizeof(pcell_t), M_AL_MSIX, M_WAITOK);
	fdt_data->hdr.type = INTR_MAP_DATA_FDT;
	fdt_data->iparent = 0;
	fdt_data->ncells = GIC_INTR_CELL_CNT;
	fdt_data->cells[0] = AL_SPI_INTR;	/* code for SPI interrupt */
	fdt_data->cells[1] = 0;			/* SPI number (uninitialized) */
	fdt_data->cells[2] = AL_EDGE_HIGH;	/* trig = edge, pol = high */

	mtx_lock(&sc->msi_mtx);

	for (i = irq_base; i < irq_base + count; i++) {
		fdt_data->cells[1] = sc->irq_min + i;
		error = PIC_MAP_INTR(sc->gic_dev,
		    (struct intr_map_data *)fdt_data, srcs);
		if (error) {
			for (j = irq_base; j < i; j++)
				sc->isrcs[j] = NULL;
			mtx_unlock(&sc->msi_mtx);
			vmem_free(sc->irq_alloc, irq_base, count);
			free(fdt_data, M_AL_MSIX);
			return (error);
		}

		sc->isrcs[i] = *srcs;
		srcs++;
	}

	mtx_unlock(&sc->msi_mtx);
	free(fdt_data, M_AL_MSIX);

	if (bootverbose)
		device_printf(dev,
		    "MSI-X allocation: start SPI %d, count %d\n",
		    (int)irq_base + sc->irq_min, count);

	*pic = sc->gic_dev;

	return (0);
}

static int
al_msix_release_msi(device_t dev, device_t child, int count,
    struct intr_irqsrc **srcs)
{
	struct al_msix_softc *sc;
	int i, pos;

	sc = device_get_softc(dev);

	mtx_lock(&sc->msi_mtx);

	pos = al_find_intr_pos_in_map(dev, *srcs);
	vmem_free(sc->irq_alloc, pos, count);
	for (i = 0; i < count; i++) {
		pos = al_find_intr_pos_in_map(dev, *srcs);
		if (pos != ERR_NOT_IN_MAP)
			sc->isrcs[pos] = NULL;
		srcs++;
	}

	mtx_unlock(&sc->msi_mtx);

	return (0);
}

static int
al_msix_alloc_msix(device_t dev, device_t child, device_t *pic,
    struct intr_irqsrc **isrcp)
{

	return (al_msix_alloc_msi(dev, child, 1, 1, pic, isrcp));
}

static int
al_msix_release_msix(device_t dev, device_t child, struct intr_irqsrc *isrc)
{

	return (al_msix_release_msi(dev, child, 1, &isrc));
}
