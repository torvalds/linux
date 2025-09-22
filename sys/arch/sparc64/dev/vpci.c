/*	$OpenBSD: vpci.c,v 1.36 2024/05/13 01:15:50 jsg Exp $	*/
/*
 * Copyright (c) 2008 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <uvm/uvm_extern.h>

#define _SPARC_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/hypervisor.h>
#include <machine/openfirm.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <sparc64/dev/viommuvar.h>
#include <sparc64/dev/msivar.h>

#define VPCI_DEFAULT_MSI_EQS	36

extern struct sparc_pci_chipset _sparc_pci_chipset;

struct vpci_msi_msg {
	uint32_t	mm_version;
	uint8_t		mm_reserved[3];
	uint8_t		mm_type;
	uint64_t	mm_sysino;
	uint64_t	mm_reserved1;
	uint64_t	mm_stick;
	uint16_t	mm_reserved2[3];
	uint16_t	mm_reqid;
	uint64_t	mm_addr;
	uint64_t	mm_data;
	uint64_t	mm_reserved3;
};

struct vpci_range {
	u_int32_t	cspace;
	u_int32_t	child_hi;
	u_int32_t	child_lo;
	u_int32_t	phys_hi;
	u_int32_t	phys_lo;
	u_int32_t	size_hi;
	u_int32_t	size_lo;
};

struct vpci_eq {
	char 			eq_name[16];
	int 			eq_id;
	void 			*eq_ih;
	struct msi_eq		*eq_meq;
	struct vpci_pbm 	*eq_pbm;
	uint8_t			*eq_ring;
	uint64_t		eq_mask;
};

struct vpci_pbm {
	struct vpci_softc *vp_sc;
	uint64_t vp_devhandle;

	struct vpci_range *vp_range;
	pci_chipset_tag_t vp_pc;
	int vp_nrange;

	bus_space_tag_t		vp_memt;
	bus_space_tag_t		vp_iot;
	bus_dma_tag_t		vp_dmat;
	struct iommu_state	vp_is;

	bus_addr_t vp_msiaddr;
	int vp_msibase;
	int vp_msinum;
	struct intrhand **vp_msi;

	unsigned int vp_neq;
	struct vpci_eq *vp_eq;

	int vp_flags;
};

struct vpci_softc {
	struct device sc_dv;
	bus_dma_tag_t sc_dmat;
	bus_space_tag_t sc_bust;
	int sc_node;
};

uint64_t sun4v_group_sdio_major;

int vpci_match(struct device *, void *, void *);
void vpci_attach(struct device *, struct device *, void *);
void vpci_init_iommu(struct vpci_softc *, struct vpci_pbm *);
void vpci_init_msi(struct vpci_softc *, struct vpci_pbm *);
int vpci_print(void *, const char *);

pci_chipset_tag_t vpci_alloc_chipset(struct vpci_pbm *, int,
    pci_chipset_tag_t);
bus_space_tag_t vpci_alloc_mem_tag(struct vpci_pbm *);
bus_space_tag_t vpci_alloc_io_tag(struct vpci_pbm *);
bus_space_tag_t vpci_alloc_bus_tag(struct vpci_pbm *, const char *,
    int, int, int);
bus_dma_tag_t vpci_alloc_dma_tag(struct vpci_pbm *);

int vpci_conf_size(pci_chipset_tag_t, pcitag_t);
pcireg_t vpci_conf_read(pci_chipset_tag_t, pcitag_t, int);
void vpci_conf_write(pci_chipset_tag_t, pcitag_t, int, pcireg_t);

int vpci_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
int vpci_intr_nomap(struct pci_attach_args *, pci_intr_handle_t *);
int vpci_bus_map(bus_space_tag_t, bus_space_tag_t, bus_addr_t,
    bus_size_t, int, bus_space_handle_t *);
paddr_t vpci_bus_mmap(bus_space_tag_t, bus_space_tag_t, bus_addr_t, off_t,
    int, int);
void *vpci_intr_establish(bus_space_tag_t, bus_space_tag_t, int, int, int,
    int (*)(void *), void *, const char *);
void *vpci_intr_establish_cpu(bus_space_tag_t, bus_space_tag_t, int, int,
    int, struct cpu_info *, int (*)(void *), void *, const char *);
void vpci_intr_ack(struct intrhand *);
void vpci_msi_ack(struct intrhand *);

int vpci_msi_eq_intr(void *);

int vpci_dmamap_create(bus_dma_tag_t, bus_dma_tag_t, bus_size_t, int,
    bus_size_t, bus_size_t, int, bus_dmamap_t *);

int
vpci_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, "pci") != 0)
		return (0);

	return (OF_is_compatible(ma->ma_node, "SUNW,sun4v-pci") ||
	    OF_is_compatible(ma->ma_node, "SUNW,sun4v-vpci"));
}

void
vpci_attach(struct device *parent, struct device *self, void *aux)
{
	struct vpci_softc *sc = (struct vpci_softc *)self;
	struct mainbus_attach_args *ma = aux;
	struct pcibus_attach_args pba;
	struct vpci_pbm *pbm;
	int *busranges = NULL, nranges;
	int virtual, intx;

	sc->sc_dmat = ma->ma_dmatag;
	sc->sc_bust = ma->ma_bustag;
	sc->sc_node = ma->ma_node;

	pbm = malloc(sizeof(*pbm), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (pbm == NULL)
		panic("vpci: can't alloc vpci pbm");

	pbm->vp_sc = sc;
	pbm->vp_devhandle = (ma->ma_reg[0].ur_paddr >> 32) & 0x0fffffff;

	if (getprop(ma->ma_node, "ranges", sizeof(struct vpci_range),
	    &pbm->vp_nrange, (void **)&pbm->vp_range))
		panic("vpci: can't get ranges");

	if (getprop(ma->ma_node, "bus-range", sizeof(int), &nranges,
	    (void **)&busranges))
		panic("vpci: can't get bus-range");

	printf(": bus %d to %d, ", busranges[0], busranges[1]);

	virtual = (OF_getproplen(ma->ma_node, "virtual-root-complex") == 0);
	intx = (OF_getproplen(ma->ma_node, "pci-intx-not-supported") != 0);

	pbm->vp_memt = vpci_alloc_mem_tag(pbm);
	pbm->vp_iot = vpci_alloc_io_tag(pbm);
	pbm->vp_dmat = vpci_alloc_dma_tag(pbm);

	pbm->vp_pc = vpci_alloc_chipset(pbm, ma->ma_node, &_sparc_pci_chipset);
	pbm->vp_pc->bustag = pbm->vp_memt;

	vpci_init_iommu(sc, pbm);
	vpci_init_msi(sc, pbm);

	bzero(&pba, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = busranges[0];
	pba.pba_pc = pbm->vp_pc;
	pba.pba_flags = pbm->vp_flags;
	pba.pba_dmat = pbm->vp_dmat;
	pba.pba_memt = pbm->vp_memt;
	pba.pba_iot = pbm->vp_iot;
	pba.pba_pc->conf_size = vpci_conf_size;
	pba.pba_pc->conf_read = vpci_conf_read;
	pba.pba_pc->conf_write = vpci_conf_write;
	pba.pba_pc->intr_map = (intx ? vpci_intr_map : vpci_intr_nomap);

	free(busranges, M_DEVBUF, 0);

	config_found(&sc->sc_dv, &pba, vpci_print);

	/* 
	 * Signal that we're ready to share this root complex with our
	 * guests.
	 */
	if (sun4v_group_sdio_major > 0 && !virtual) {
		int err;

		err = hv_pci_iov_root_configured(pbm->vp_devhandle);
		if (err != H_EOK) {
			printf("%s: pci_iov_root_configured: err %x\n",
			       sc->sc_dv.dv_xname, err);
		}
			       
	}
}

void
vpci_init_iommu(struct vpci_softc *sc, struct vpci_pbm *pbm)
{
	struct iommu_state *is = &pbm->vp_is;
	int tsbsize = 8;
	u_int32_t iobase = 0x80000000;
	char *name;

	name = (char *)malloc(32, M_DEVBUF, M_NOWAIT);
	if (name == NULL)
		panic("couldn't malloc iommu name");
	snprintf(name, 32, "%s dvma", sc->sc_dv.dv_xname);

	viommu_init(name, is, tsbsize, iobase);
	is->is_devhandle = pbm->vp_devhandle;
}

void
vpci_init_msi(struct vpci_softc *sc, struct vpci_pbm *pbm)
{
	u_int32_t msi_addr_range[3];
	u_int32_t msi_eq_devino[3] = { 0, 36, 24 };
	u_int32_t msi_range[2];
	uint64_t sysino;
	int msis, msi_eq_size, num_eq, unit;
	struct vpci_eq *eq;
	int err;
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;

	/* One eq per cpu, limited by the number of eqs. */
	num_eq = min(ncpus, getpropint(sc->sc_node, "#msi-eqs", 36));
	if (num_eq == 0)
		return;

	if (OF_getprop(sc->sc_node, "msi-address-ranges",
	    msi_addr_range, sizeof(msi_addr_range)) <= 0)
		return;
	pbm->vp_msiaddr = msi_addr_range[1];
	pbm->vp_msiaddr |= ((bus_addr_t)msi_addr_range[0]) << 32;

	msis = getpropint(sc->sc_node, "#msi", 256);
	pbm->vp_msi = mallocarray(msis, sizeof(*pbm->vp_msi), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (pbm->vp_msi == NULL)
		return;

	msi_eq_size = getpropint(sc->sc_node, "msi-eq-size", 256);

	if (OF_getprop(sc->sc_node, "msi-ranges",
	    msi_range, sizeof(msi_range)) <= 0)
		goto free_table;
	pbm->vp_msibase = msi_range[0];

	pbm->vp_neq = num_eq;
	pbm->vp_eq = mallocarray(num_eq, sizeof(*eq), M_DEVBUF,
	    M_WAITOK | M_ZERO);

	CPU_INFO_FOREACH(cii, ci) {
		unit = CPU_INFO_UNIT(ci);
		eq = &pbm->vp_eq[unit];

		if (unit >= num_eq)
			continue;

		eq->eq_id = unit;
		eq->eq_pbm = pbm;
		snprintf(eq->eq_name, sizeof(eq->eq_name), "%s:%d",
		    sc->sc_dv.dv_xname, unit);

		eq->eq_meq = msi_eq_alloc(sc->sc_dmat, msi_eq_size, 1);
		if (eq->eq_meq == NULL)
			goto free_queues;

		err = hv_pci_msiq_conf(pbm->vp_devhandle, unit,
		    eq->eq_meq->meq_map->dm_segs[0].ds_addr,
		    eq->eq_meq->meq_nentries);
		if (err != H_EOK)
			goto free_queues;

		eq->eq_mask = (eq->eq_meq->meq_nentries *
		    sizeof(struct vpci_msi_msg)) - 1;

		OF_getprop(sc->sc_node, "msi-eq-to-devino",
		    msi_eq_devino, sizeof(msi_eq_devino));
		err = sun4v_intr_devino_to_sysino(pbm->vp_devhandle,
		    msi_eq_devino[2] + unit, &sysino);
		if (err != H_EOK)
			goto free_queues;

		eq->eq_ih = vpci_intr_establish_cpu(pbm->vp_memt, pbm->vp_memt,
		    sysino, IPL_HIGH, BUS_INTR_ESTABLISH_MPSAFE, ci,
		    vpci_msi_eq_intr, eq, eq->eq_name);
		if (eq->eq_ih == NULL)
			goto free_queues;

		err = hv_pci_msiq_setvalid(pbm->vp_devhandle, unit,
		    PCI_MSIQ_VALID);
		if (err != H_EOK) {
			printf("%s: pci_msiq_setvalid(%d): err %d\n", __func__,
			    unit, err);
			goto free_queues;
		}

		err = hv_pci_msiq_setstate(pbm->vp_devhandle, unit,
		    PCI_MSIQSTATE_IDLE);
		if (err != H_EOK) {
			printf("%s: pci_msiq_setstate(%d): err %d\n", __func__,
			    unit, err);
			goto free_queues;
		}
	}

	pbm->vp_flags |= PCI_FLAGS_MSI_ENABLED;
	return;

free_queues:
	CPU_INFO_FOREACH(cii, ci) {
		unit = CPU_INFO_UNIT(ci);
		eq = &pbm->vp_eq[unit];

		if (eq->eq_meq != NULL)
			msi_eq_free(sc->sc_dmat, eq->eq_meq);

		hv_pci_msiq_conf(pbm->vp_devhandle, unit, 0, 0);
	}
	free(pbm->vp_eq, M_DEVBUF, num_eq * sizeof(*eq));
free_table:
	free(pbm->vp_msi, M_DEVBUF, 0);
}

int
vpci_print(void *aux, const char *p)
{
	if (p == NULL)
		return (UNCONF);
	return (QUIET);
}

int
vpci_conf_size(pci_chipset_tag_t pc, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
vpci_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
	struct vpci_pbm *pbm = pc->cookie;
	uint64_t error_flag, data;

	hv_pci_config_get(pbm->vp_devhandle, PCITAG_OFFSET(tag), reg, 4,
	    &error_flag, &data);

	return (error_flag ? (pcireg_t)~0 : data);
}

void
vpci_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, pcireg_t data)
{
	struct vpci_pbm *pbm = pc->cookie;
	uint64_t error_flag;

	hv_pci_config_put(pbm->vp_devhandle, PCITAG_OFFSET(tag), reg, 4,
            data, &error_flag);
}

/*
 * Bus-specific interrupt mapping
 */
int
vpci_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	struct vpci_pbm *pbm = pa->pa_pc->cookie;
	uint64_t devhandle = pbm->vp_devhandle;
	uint64_t devino, sysino;
	int err;

	/*
	 * If we didn't find a PROM mapping for this interrupt.  Try
	 * to construct one ourselves based on the swizzled interrupt
	 * pin.
	 */
	if (*ihp == (pci_intr_handle_t)-1 && pa->pa_intrpin != 0)
		*ihp = pa->pa_intrpin;

	if (*ihp != (pci_intr_handle_t)-1) {
		devino = INTVEC(*ihp);
		err = sun4v_intr_devino_to_sysino(devhandle, devino, &sysino);
		if (err != H_EOK)
			return (-1);

		*ihp = sysino;
		return (0);
	}

	return (-1);
}

int
vpci_intr_nomap(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	return (-1);
}

bus_space_tag_t
vpci_alloc_mem_tag(struct vpci_pbm *pp)
{
	return (vpci_alloc_bus_tag(pp, "mem",
	    0x02,       /* 32-bit mem space (where's the #define???) */
	    ASI_PRIMARY, ASI_PRIMARY_LITTLE));
}

bus_space_tag_t
vpci_alloc_io_tag(struct vpci_pbm *pp)
{
	return (vpci_alloc_bus_tag(pp, "io",
	    0x01,       /* IO space (where's the #define???) */
	    ASI_PRIMARY, ASI_PRIMARY_LITTLE));
}

bus_space_tag_t
vpci_alloc_bus_tag(struct vpci_pbm *pbm, const char *name, int ss,
    int asi, int sasi)
{
	struct vpci_softc *sc = pbm->vp_sc;
	struct sparc_bus_space_tag *bt;

	bt = malloc(sizeof(*bt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (bt == NULL)
		panic("vpci: could not allocate bus tag");

	snprintf(bt->name, sizeof(bt->name), "%s-pbm_%s(%d/%2.2x)",
	    sc->sc_dv.dv_xname, name, ss, asi);

	bt->cookie = pbm;
	bt->parent = sc->sc_bust;
	bt->default_type = ss;
	bt->asi = asi;
	bt->sasi = sasi;
	bt->sparc_bus_map = vpci_bus_map;
	bt->sparc_bus_mmap = vpci_bus_mmap;
	bt->sparc_intr_establish = vpci_intr_establish;
	bt->sparc_intr_establish_cpu = vpci_intr_establish_cpu;
	return (bt);
}

bus_dma_tag_t
vpci_alloc_dma_tag(struct vpci_pbm *pbm)
{
	struct vpci_softc *sc = pbm->vp_sc;
	bus_dma_tag_t dt, pdt = sc->sc_dmat;

	dt = malloc(sizeof(*dt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (dt == NULL)
		panic("vpci: could not alloc dma tag");

	dt->_cookie = pbm;
	dt->_parent = pdt;
	dt->_dmamap_create = vpci_dmamap_create;
	dt->_dmamap_destroy = viommu_dvmamap_destroy;
	dt->_dmamap_load = viommu_dvmamap_load;
	dt->_dmamap_load_raw = viommu_dvmamap_load_raw;
	dt->_dmamap_unload = viommu_dvmamap_unload;
	dt->_dmamap_sync = viommu_dvmamap_sync;
	dt->_dmamem_alloc = viommu_dvmamem_alloc;
	dt->_dmamem_free = viommu_dvmamem_free;
	return (dt);
}

pci_chipset_tag_t
vpci_alloc_chipset(struct vpci_pbm *pbm, int node, pci_chipset_tag_t pc)
{
	pci_chipset_tag_t npc;

	npc = malloc(sizeof *npc, M_DEVBUF, M_NOWAIT);
	if (npc == NULL)
		panic("vpci: could not allocate pci_chipset_tag_t");
	memcpy(npc, pc, sizeof *pc);
	npc->cookie = pbm;
	npc->rootnode = node;
	return (npc);
}

#define BUS_DMA_FIND_PARENT(t, fn)                                      \
        if (t->_parent == NULL)                                         \
                panic("null bus_dma parent (" #fn ")");                 \
        for (t = t->_parent; t->fn == NULL; t = t->_parent)             \
                if (t->_parent == NULL)                                 \
                        panic("no bus_dma " #fn " located");

int
vpci_dmamap_create(bus_dma_tag_t t, bus_dma_tag_t t0, bus_size_t size,
    int nsegments, bus_size_t maxsegsz, bus_size_t boundary, int flags,
    bus_dmamap_t *dmamap)
{
	struct vpci_pbm *vp = t->_cookie;

	return (viommu_dvmamap_create(t, t0, &vp->vp_is, size, nsegments,
	    maxsegsz, boundary, flags, dmamap));
}

int
vpci_bus_map(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t offset,
    bus_size_t size, int flags, bus_space_handle_t *hp)
{
	struct vpci_pbm *pbm = t->cookie;
	int i, ss = t->default_type;

	if (t->parent == 0 || t->parent->sparc_bus_map == 0)
		panic("vpci_bus_map: invalid parent");

	if (flags & BUS_SPACE_MAP_PROMADDRESS) {
		return ((*t->parent->sparc_bus_map)
		    (t, t0, offset, size, flags, hp));
	}

	/* Check mappings for 64-bit-address Memory Space if appropriate. */
	if (ss == 0x02 && offset > 0xffffffff)
		ss = 0x03;

	for (i = 0; i < pbm->vp_nrange; i++) {
		bus_addr_t child, paddr;
		bus_size_t rsize;

		if (((pbm->vp_range[i].cspace >> 24) & 0x03) != ss)
			continue;
		child = pbm->vp_range[i].child_lo;
		child |= ((bus_addr_t)pbm->vp_range[i].child_hi) << 32;
		rsize = pbm->vp_range[i].size_lo;
		rsize |= ((bus_size_t)pbm->vp_range[i].size_hi) << 32;
		if (offset < child || offset >= child + rsize)
			continue;

		paddr = pbm->vp_range[i].phys_lo;
		paddr |= ((bus_addr_t)pbm->vp_range[i].phys_hi) << 32;
		return ((*t->parent->sparc_bus_map)
		    (t, t0, paddr + offset - child, size, flags, hp));
	}

	return (EINVAL);
}

paddr_t
vpci_bus_mmap(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t paddr,
    off_t off, int prot, int flags)
{
	bus_addr_t offset = paddr;
	struct vpci_pbm *pbm = t->cookie;
	int i, ss = t->default_type;

	if (t->parent == 0 || t->parent->sparc_bus_mmap == 0)
		panic("vpci_bus_mmap: invalid parent");

	for (i = 0; i < pbm->vp_nrange; i++) {
		bus_addr_t paddr;

		if (((pbm->vp_range[i].cspace >> 24) & 0x03) != ss)
			continue;

		paddr = pbm->vp_range[i].phys_lo + offset;
		paddr |= ((bus_addr_t)pbm->vp_range[i].phys_hi) << 32;
		return ((*t->parent->sparc_bus_mmap)
		    (t, t0, paddr, off, prot, flags));
	}

	return (-1);
}

void *
vpci_intr_establish(bus_space_tag_t t, bus_space_tag_t t0, int ihandle,
    int level, int flags, int (*handler)(void *), void *arg, const char *what)
{
	return (vpci_intr_establish_cpu(t, t0, ihandle, level, flags, NULL,
	    handler, arg, what));
}

void *
vpci_intr_establish_cpu(bus_space_tag_t t, bus_space_tag_t t0, int ihandle,
    int level, int flags, struct cpu_info *cpu, int (*handler)(void *),
    void *arg, const char *what)
{
	struct vpci_pbm *pbm = t->cookie;
	uint64_t devhandle = pbm->vp_devhandle;
	uint64_t sysino = INTVEC(ihandle);
	struct intrhand *ih;
	int err;

	ih = bus_intr_allocate(t0, handler, arg, ihandle, level,
	    NULL, NULL, what);
	if (ih == NULL)
		return (NULL);

	if (flags & BUS_INTR_ESTABLISH_MPSAFE)
		ih->ih_mpsafe = 1;

	if (PCI_INTR_TYPE(ihandle) != PCI_INTR_INTX) {
		pci_chipset_tag_t pc = pbm->vp_pc;
		pcitag_t tag = PCI_INTR_TAG(ihandle);
		int msinum = pbm->vp_msinum++;
		int msi = pbm->vp_msibase + msinum;
		int eq = 0;

		evcount_attach(&ih->ih_count, ih->ih_name, NULL);

		ih->ih_ack = vpci_msi_ack;

		pbm->vp_msi[msinum] = ih;
		ih->ih_number = msinum;

		switch (PCI_INTR_TYPE(ihandle)) {
		case PCI_INTR_MSI:
			pci_msi_enable(pc, tag, pbm->vp_msiaddr, msi);
			break;
		case PCI_INTR_MSIX:
			pci_msix_enable(pc, tag, pbm->vp_memt,
			    PCI_INTR_VEC(ihandle), pbm->vp_msiaddr, msi);
			break;
		}

		if (cpu != NULL) {
			/*
			 * For now, if we have fewer eqs than cpus, map
			 * interrupts for the eq-less cpus onto other cpus.
			 */
			eq = CPU_INFO_UNIT(cpu) % pbm->vp_neq;
		}

		err = hv_pci_msi_setmsiq(devhandle, msi, eq, 0);
		if (err != H_EOK) {
			printf("%s: pci_msi_setmsiq: err %d\n", __func__, err);
			return (NULL);
		}

		err = hv_pci_msi_setvalid(devhandle, msi, PCI_MSI_VALID);
		if (err != H_EOK) {
			printf("%s: pci_msi_setvalid: err %d\n", __func__, err);
			return (NULL);
		}

		err = hv_pci_msi_setstate(devhandle, msi, PCI_MSISTATE_IDLE);
		if (err != H_EOK) {
			printf("%s: pci_msi_setstate: err %d\n", __func__, err);
			return (NULL);
		}

		return (ih);
	}

	err = sun4v_intr_setcookie(devhandle, sysino, (vaddr_t)ih);
	if (err != H_EOK)
		return (NULL);

	ih->ih_cpu = cpu;
	intr_establish(ih);
	ih->ih_ack = vpci_intr_ack;

	err = sun4v_intr_settarget(devhandle, sysino, ih->ih_cpu->ci_upaid);
	if (err != H_EOK)
		return (NULL);

	/* Clear pending interrupts. */
	err = sun4v_intr_setstate(devhandle, sysino, INTR_IDLE);
	if (err != H_EOK)
		return (NULL);

	err = sun4v_intr_setenabled(devhandle, sysino, INTR_ENABLED);
	if (err != H_EOK)
		return (NULL);

	return (ih);
}

void
vpci_intr_ack(struct intrhand *ih)
{
	bus_space_tag_t t = ih->ih_bus;
	struct vpci_pbm *pbm = t->cookie;
	uint64_t devhandle = pbm->vp_devhandle;
	uint64_t sysino = INTVEC(ih->ih_number);
	
	sun4v_intr_setstate(devhandle, sysino, INTR_IDLE);
}

void
vpci_msi_ack(struct intrhand *ih)
{
}

int
vpci_msi_eq_intr(void *arg)
{
	struct vpci_eq *eq = arg;
	struct vpci_pbm *pbm = eq->eq_pbm;
	struct vpci_msi_msg *msg;
	uint64_t devhandle = pbm->vp_devhandle;
	uint64_t head, tail;
	struct intrhand *ih;
	int msinum, msi;
	int err;

	err = hv_pci_msiq_gethead(devhandle, eq->eq_id, &head);
	if (err != H_EOK)
		printf("%s: pci_msiq_gethead(%d): %d\n", __func__, eq->eq_id,
		    err);

	err = hv_pci_msiq_gettail(devhandle, eq->eq_id, &tail);
	if (err != H_EOK)
		printf("%s: pci_msiq_gettail(%d): %d\n", __func__, eq->eq_id,
		    err);

	if (head == tail)
		return (0);

	while (head != tail) {
		msg = (struct vpci_msi_msg *)(eq->eq_meq->meq_va + head);

		if (msg->mm_type == 0)
			break;
		msg->mm_type = 0;

		msi = msg->mm_data;
		msinum = msi - pbm->vp_msibase;
		ih = pbm->vp_msi[msinum];
		err = hv_pci_msi_setstate(devhandle, msi, PCI_MSISTATE_IDLE);
		if (err != H_EOK)
			printf("%s: pci_msi_setstate: %d\n", __func__, err);

		send_softint(ih->ih_pil, ih);

		head += sizeof(struct vpci_msi_msg);
		head &= eq->eq_mask;
	}

	err = hv_pci_msiq_sethead(devhandle, eq->eq_id, head);
	if (err != H_EOK)
		printf("%s: pci_msiq_sethead(%d): %d\n", __func__, eq->eq_id,
		    err);

	return (1);
}

const struct cfattach vpci_ca = {
	sizeof(struct vpci_softc), vpci_match, vpci_attach
};

struct cfdriver vpci_cd = {
	NULL, "vpci", DV_DULL
};
