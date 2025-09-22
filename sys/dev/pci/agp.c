/* $OpenBSD: agp.c,v 1.51 2024/05/24 06:02:53 jsg Exp $ */
/*-
 * Copyright (c) 2000 Doug Rabson
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
 *
 *	$FreeBSD: src/sys/pci/agp.c,v 1.12 2001/05/19 01:28:07 alfred Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <dev/pci/pcivar.h>

#include <dev/pci/agpvar.h>
#include <dev/pci/agpreg.h>

void	agp_attach(struct device *, struct device *, void *);
int	agp_probe(struct device *, void *, void *);

int	agpvga_match(struct pci_attach_args *);

int
agpdev_print(void *aux, const char *pnp)
{
	if (pnp) {
		printf("agp at %s", pnp);
	}
	return (UNCONF);
}

int
agpbus_probe(struct agp_attach_args *aa)
{
	struct pci_attach_args	*pa = aa->aa_pa;

	if (strncmp(aa->aa_busname, "agp", 3) == 0 &&
	    PCI_CLASS(pa->pa_class) == PCI_CLASS_BRIDGE && 
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_BRIDGE_HOST)
		return (1);
	return (0);
}

/*
 * Find the video card hanging off the agp bus XXX assumes only one bus
 */
int
agpvga_match(struct pci_attach_args *pa)
{
	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_DISPLAY &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_DISPLAY_VGA) {
		if (pci_get_capability(pa->pa_pc, pa->pa_tag, PCI_CAP_AGP,
		    NULL, NULL))
			return (1);
	}
	return (0);
}

struct device *
agp_attach_bus(struct pci_attach_args *pa, const struct agp_methods *methods,
    bus_addr_t apaddr, bus_size_t apsize, struct device *dev)
{
	struct agpbus_attach_args arg;

	arg.aa_methods = methods;
	arg.aa_pa = pa;
	arg.aa_apaddr = apaddr;
	arg.aa_apsize = apsize;

	printf("\n"); /* newline from the driver that called us */
	return (config_found(dev, &arg, agpdev_print));
}

int
agp_probe(struct device *parent, void *match, void *aux)
{
	/*
	 * we don't do any checking here, driver we're attaching this
	 * interface to should have already done it.
	 */
	return (1);
}

void
agp_attach(struct device *parent, struct device *self, void *aux)
{
	struct agpbus_attach_args *aa = aux;
	struct pci_attach_args *pa = aa->aa_pa;
	struct agp_softc *sc = (struct agp_softc *)self;
	u_int memsize;
	int i;

	sc->sc_chipc = parent;
	sc->sc_methods = aa->aa_methods;
	sc->sc_apaddr = aa->aa_apaddr;
	sc->sc_apsize = aa->aa_apsize;

	static const int agp_max[][2] = {
		{0,		0},
		{32,		4},
		{64,		28},
		{128,		96},
		{256,		204},
		{512,		440},
		{1024,		942},
		{2048,		1920},
		{4096,		3932}
	};

	/*
	 * Work out an upper bound for agp memory allocation. This
	 * uses a heuristic table from the Linux driver.
	 */
	memsize = ptoa(physmem) >> 20;

	for (i = 0; i < nitems(agp_max) && memsize > agp_max[i][0]; i++)
		;
	if (i == nitems(agp_max))
		i = nitems(agp_max) - 1;
	sc->sc_maxmem = agp_max[i][1] << 20;

	sc->sc_pcitag = pa->pa_tag;
	sc->sc_pc = pa->pa_pc;
	sc->sc_id = pa->pa_id;
	sc->sc_dmat = pa->pa_dmat;
	sc->sc_memt = pa->pa_memt;

	pci_get_capability(sc->sc_pc, sc->sc_pcitag, PCI_CAP_AGP,
	    &sc->sc_capoff, NULL);

	printf(": aperture at 0x%lx, size 0x%lx\n", (u_long)sc->sc_apaddr,
	    (u_long)sc->sc_apsize);
}

const struct cfattach agp_ca = {
	sizeof(struct agp_softc), agp_probe, agp_attach,
	NULL, NULL
};

struct cfdriver agp_cd = {
	NULL, "agp", DV_DULL
};

struct agp_gatt *
agp_alloc_gatt(bus_dma_tag_t dmat, u_int32_t apsize)
{
	struct agp_gatt		*gatt;
	u_int32_t	 	 entries = apsize >> AGP_PAGE_SHIFT;

	gatt = malloc(sizeof(*gatt), M_AGP, M_NOWAIT | M_ZERO);
	if (!gatt)
		return (NULL);
	gatt->ag_entries = entries;
	gatt->ag_size = entries * sizeof(u_int32_t);

	if (agp_alloc_dmamem(dmat, gatt->ag_size, &gatt->ag_dmamap,
	    &gatt->ag_physical, &gatt->ag_dmaseg) != 0) {
		free(gatt, M_AGP, sizeof *gatt);
		return (NULL);
	}

	if (bus_dmamem_map(dmat, &gatt->ag_dmaseg, 1, gatt->ag_size,
	    (caddr_t *)&gatt->ag_virtual, BUS_DMA_NOWAIT) != 0) {
		agp_free_dmamem(dmat, gatt->ag_size, gatt->ag_dmamap,
		    &gatt->ag_dmaseg);
		free(gatt, M_AGP, sizeof *gatt);
		return (NULL);
	}

	agp_flush_cache();

	return (gatt);
}

void
agp_free_gatt(bus_dma_tag_t dmat, struct agp_gatt *gatt)
{
	bus_dmamem_unmap(dmat, (caddr_t)gatt->ag_virtual, gatt->ag_size);
	agp_free_dmamem(dmat, gatt->ag_size, gatt->ag_dmamap, &gatt->ag_dmaseg);
	free(gatt, M_AGP, sizeof *gatt);
}

int
agp_generic_enable(struct agp_softc *sc, u_int32_t mode)
{
	struct pci_attach_args	pa;
	pcireg_t		tstatus, mstatus, command;
	int			rq, sba, fw, rate, capoff;
	
	if (pci_find_device(&pa, agpvga_match) == 0 ||
	    pci_get_capability(pa.pa_pc, pa.pa_tag, PCI_CAP_AGP,
	    &capoff, NULL) == 0) {
		printf("agp_generic_enable: not an AGP capable device\n");
		return (-1);
	}

	tstatus = pci_conf_read(sc->sc_pc, sc->sc_pcitag,
	    sc->sc_capoff + AGP_STATUS);
	/* display agp mode */
	mstatus = pci_conf_read(pa.pa_pc, pa.pa_tag,
	    capoff + AGP_STATUS);

	/* Set RQ to the min of mode, tstatus and mstatus */
	rq = AGP_MODE_GET_RQ(mode);
	if (AGP_MODE_GET_RQ(tstatus) < rq)
		rq = AGP_MODE_GET_RQ(tstatus);
	if (AGP_MODE_GET_RQ(mstatus) < rq)
		rq = AGP_MODE_GET_RQ(mstatus);

	/* Set SBA if all three can deal with SBA */
	sba = (AGP_MODE_GET_SBA(tstatus)
	    & AGP_MODE_GET_SBA(mstatus)
	    & AGP_MODE_GET_SBA(mode));

	/* Similar for FW */
	fw = (AGP_MODE_GET_FW(tstatus)
	    & AGP_MODE_GET_FW(mstatus)
	    & AGP_MODE_GET_FW(mode));

	/* Figure out the max rate */
	rate = (AGP_MODE_GET_RATE(tstatus)
	    & AGP_MODE_GET_RATE(mstatus)
	    & AGP_MODE_GET_RATE(mode));
	if (rate & AGP_MODE_RATE_4x)
		rate = AGP_MODE_RATE_4x;
	else if (rate & AGP_MODE_RATE_2x)
		rate = AGP_MODE_RATE_2x;
	else
		rate = AGP_MODE_RATE_1x;

	/* Construct the new mode word and tell the hardware  */
	command = AGP_MODE_SET_RQ(0, rq);
	command = AGP_MODE_SET_SBA(command, sba);
	command = AGP_MODE_SET_FW(command, fw);
	command = AGP_MODE_SET_RATE(command, rate);
	command = AGP_MODE_SET_AGP(command, 1);

	pci_conf_write(sc->sc_pc, sc->sc_pcitag,
	    sc->sc_capoff + AGP_COMMAND, command);
	pci_conf_write(pa.pa_pc, pa.pa_tag, capoff + AGP_COMMAND, command);
	return (0);
}

/*
 * Allocates a single-segment block of zeroed, wired dma memory.
 */
int
agp_alloc_dmamem(bus_dma_tag_t tag, size_t size, bus_dmamap_t *mapp,
    bus_addr_t *baddr, bus_dma_segment_t *seg)
{
	int error, level = 0, nseg;

	if ((error = bus_dmamem_alloc(tag, size, PAGE_SIZE, 0,
	    seg, 1, &nseg, BUS_DMA_NOWAIT | BUS_DMA_ZERO)) != 0)
		goto out;
	level++;

	if ((error = bus_dmamap_create(tag, size, nseg, size, 0,
	    BUS_DMA_NOWAIT, mapp)) != 0)
		goto out;
	level++;

	if ((error = bus_dmamap_load_raw(tag, *mapp, seg, nseg, size,
	    BUS_DMA_NOWAIT)) != 0)
		goto out;

	*baddr = (*mapp)->dm_segs[0].ds_addr;

	return (0);
out:
	switch (level) {
	case 2:
		bus_dmamap_destroy(tag, *mapp);
		/* FALLTHROUGH */
	case 1:
		bus_dmamem_free(tag, seg, nseg);
		break;
	default:
		break;
	}

	return (error);
}

void
agp_free_dmamem(bus_dma_tag_t tag, size_t size, bus_dmamap_t map,
    bus_dma_segment_t *seg)
{
	bus_dmamap_unload(tag, map);
	bus_dmamap_destroy(tag, map);
	bus_dmamem_free(tag, seg, 1);
}

/* Implementation of the kernel api */

void *
agp_find_device(int unit)
{
	if (unit >= agp_cd.cd_ndevs || unit < 0)
		return (NULL);
	return (agp_cd.cd_devs[unit]);
}

enum agp_acquire_state
agp_state(void *dev)
{
	struct agp_softc *sc = (struct agp_softc *) dev;
        return (sc->sc_state);
}

void
agp_get_info(void *dev, struct agp_info *info)
{
	struct agp_softc *sc = (struct agp_softc *)dev;

	if (sc->sc_capoff != 0)
		info->ai_mode = pci_conf_read(sc->sc_pc, sc->sc_pcitag,
		    AGP_STATUS + sc->sc_capoff);
	else
		info->ai_mode = 0; /* i810 doesn't have real AGP */
	info->ai_aperture_base = sc->sc_apaddr;
	info->ai_aperture_size = sc->sc_apsize;
	info->ai_memory_allowed = sc->sc_maxmem;
	info->ai_memory_used = sc->sc_allocated;
	info->ai_devid = sc->sc_id;
}

int
agp_acquire(void *dev)
{
	struct agp_softc *sc = (struct agp_softc *)dev;

	if (sc->sc_chipc == NULL) 
		return (EINVAL);

	if (sc->sc_state != AGP_ACQUIRE_FREE)
		return (EBUSY);
	sc->sc_state = AGP_ACQUIRE_KERNEL;

	return (0);
}

int
agp_release(void *dev)
{
	struct agp_softc *sc = (struct agp_softc *)dev;

	if (sc->sc_state == AGP_ACQUIRE_FREE)
		return (0);

	if (sc->sc_state != AGP_ACQUIRE_KERNEL) 
		return (EBUSY);

	sc->sc_state = AGP_ACQUIRE_FREE;
	return (0);
}

int
agp_enable(void *dev, u_int32_t mode)
{
	struct agp_softc	*sc = dev;
	int			 ret;

	if (sc->sc_methods->enable != NULL) {
		ret = sc->sc_methods->enable(sc->sc_chipc, mode);
	} else {
		ret = agp_generic_enable(sc, mode);
	}
	return (ret);
}

paddr_t
agp_mmap(struct agp_softc *sc, off_t off, int prot)
{
	if (sc->sc_chipc == NULL)
		return (-1);

	if (off >= sc->sc_apsize)
		return (-1);

	if (sc->sc_apaddr == 0)
		return (-1);

	return bus_space_mmap(sc->sc_memt, sc->sc_apaddr, off, prot, 0);
}
