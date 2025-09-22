/*	$OpenBSD: astro.c,v 1.19 2024/04/13 23:44:11 jsg Exp $	*/

/*
 * Copyright (c) 2007 Mark Kettenis
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
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/extent.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/reboot.h>
#include <sys/tree.h>

#include <uvm/uvm_extern.h>

#include <machine/iomod.h>
#include <machine/autoconf.h>

#include <hppa/dev/cpudevs.h>

struct astro_regs {
	u_int32_t	rid;
	u_int32_t	pad0000;
	u_int32_t	ioc_ctrl;
	u_int32_t	pad0008;
	u_int8_t	resv1[0x0300 - 0x0010];
	u_int64_t	lmmio_direct0_base;
	u_int64_t	lmmio_direct0_mask;
	u_int64_t	lmmio_direct0_route;
	u_int64_t	lmmio_direct1_base;
	u_int64_t	lmmio_direct1_mask;
	u_int64_t	lmmio_direct1_route;
	u_int64_t	lmmio_direct2_base;
	u_int64_t	lmmio_direct2_mask;
	u_int64_t	lmmio_direct2_route;
	u_int64_t	lmmio_direct3_base;
	u_int64_t	lmmio_direct3_mask;
	u_int64_t	lmmio_direct3_route;
	u_int64_t	lmmio_dist_base;
	u_int64_t	lmmio_dist_mask;
	u_int64_t	lmmio_dist_route;
	u_int64_t	gmmio_dist_base;
	u_int64_t	gmmio_dist_mask;
	u_int64_t	gmmio_dist_route;
	u_int64_t	ios_dist_base;
	u_int64_t	ios_dist_mask;
	u_int64_t	ios_dist_route;
	u_int8_t	resv2[0x03c0 - 0x03a8];
	u_int64_t	ios_direct_base;
	u_int64_t	ios_direct_mask;
	u_int64_t	ios_direct_route;
	u_int8_t	resv3[0x22000 - 0x03d8];
	u_int64_t	func_id;
	u_int64_t	func_class;
	u_int8_t	resv4[0x22040 - 0x22010];
	u_int64_t	rope_config;
	u_int8_t	resv5[0x22050 - 0x22048];
	u_int64_t	rope_debug;
	u_int8_t	resv6[0x22200 - 0x22058];
	u_int64_t	rope0_control;
	u_int64_t	rope1_control;
	u_int64_t	rope2_control;
	u_int64_t	rope3_control;
	u_int64_t	rope4_control;
	u_int64_t	rope5_control;
	u_int64_t	rope6_control;
	u_int64_t	rope7_control;
	u_int8_t	resv7[0x22300 - 0x22240];
	u_int32_t	tlb_ibase;
	u_int32_t	pad22300;
	u_int32_t	tlb_imask;
	u_int32_t	pad22308;
	u_int32_t	tlb_pcom;
	u_int32_t	pad22310;
	u_int32_t	tlb_tcnfg;
	u_int32_t	pad22318;
	u_int64_t	tlb_pdir_base;
};

#define ASTRO_IOC_CTRL_TE	0x0001	/* TOC Enable */
#define ASTRO_IOC_CTRL_CE	0x0002	/* Coalesce Enable */
#define ASTRO_IOC_CTRL_DE	0x0004	/* Dillon Enable */
#define ASTRO_IOC_CTRL_IE	0x0008	/* IOS Enable */
#define ASTRO_IOC_CTRL_OS	0x0010	/* Outbound Synchronous */
#define ASTRO_IOC_CTRL_IS	0x0020	/* Inbound Synchronous */
#define ASTRO_IOC_CTRL_RC	0x0040	/* Read Current Enable */
#define ASTRO_IOC_CTRL_L0	0x0080	/* 0-length Read Enable */
#define ASTRO_IOC_CTRL_RM	0x0100	/* Real Mode */
#define ASTRO_IOC_CTRL_NC	0x0200	/* Non-coherent Mode */
#define ASTRO_IOC_CTRL_ID	0x0400	/* Interrupt Disable */
#define ASTRO_IOC_CTRL_D4	0x0800	/* Disable 4-byte Coalescing */
#define ASTRO_IOC_CTRL_CC	0x1000	/* Increase Coalescing counter value */
#define ASTRO_IOC_CTRL_DD	0x2000	/* Disable distr. range coalescing */
#define ASTRO_IOC_CTRL_DC	0x4000	/* Disable the coalescing counter */

#define IOTTE_V		0x8000000000000000LL	/* Entry valid */
#define IOTTE_PAMASK	0x000000fffffff000LL
#define IOTTE_CI	0x00000000000000ffLL	/* Coherent index */

struct astro_softc {
	struct device sc_dv;

	bus_dma_tag_t sc_dmat;
	struct astro_regs volatile *sc_regs;
	u_int64_t *sc_pdir;

	char sc_dvmamapname[20];
	struct extent *sc_dvmamap;
	struct mutex sc_dvmamtx;

	struct hppa_bus_dma_tag sc_dmatag;
};

/*
 * per-map DVMA page table
 */
struct iommu_page_entry {
	SPLAY_ENTRY(iommu_page_entry) ipe_node;
	paddr_t	ipe_pa;
	vaddr_t	ipe_va;
	bus_addr_t ipe_dva;
};

struct iommu_page_map {
	SPLAY_HEAD(iommu_page_tree, iommu_page_entry) ipm_tree;
	int ipm_maxpage;	/* Size of allocated page map */
	int ipm_pagecnt;	/* Number of entries in use */
	struct iommu_page_entry	ipm_map[1];
};

/*
 * per-map IOMMU state
 */
struct iommu_map_state {
	struct astro_softc *ims_sc;
	bus_addr_t ims_dvmastart;
	bus_size_t ims_dvmasize;
	struct extent_region ims_er;
	struct iommu_page_map ims_map;	/* map must be last (array at end) */
};

int	astro_match(struct device *, void *, void *);
void	astro_attach(struct device *, struct device *, void *);

const struct cfattach astro_ca = {
	sizeof(struct astro_softc), astro_match, astro_attach
};

struct cfdriver astro_cd = {
	NULL, "astro", DV_DULL
};

int	iommu_dvmamap_create(void *, bus_size_t, int, bus_size_t, bus_size_t,
	    int, bus_dmamap_t *);
void	iommu_dvmamap_destroy(void *, bus_dmamap_t);
int	iommu_dvmamap_load(void *, bus_dmamap_t, void *, bus_size_t,
	    struct proc *, int);
int	iommu_iomap_load_map(struct astro_softc *, bus_dmamap_t, int);
int	iommu_dvmamap_load_mbuf(void *, bus_dmamap_t, struct mbuf *, int);
int	iommu_dvmamap_load_uio(void *, bus_dmamap_t, struct uio *, int);
int	iommu_dvmamap_load_raw(void *, bus_dmamap_t, bus_dma_segment_t *,
	    int, bus_size_t, int);
void	iommu_dvmamap_unload(void *, bus_dmamap_t);
void	iommu_dvmamap_sync(void *, bus_dmamap_t, bus_addr_t, bus_size_t, int);
int	iommu_dvmamem_alloc(void *, bus_size_t, bus_size_t, bus_size_t,
	    bus_dma_segment_t *, int, int *, int);
void	iommu_dvmamem_free(void *, bus_dma_segment_t *, int);
int	iommu_dvmamem_map(void *, bus_dma_segment_t *, int, size_t,
	    caddr_t *, int);
void	iommu_dvmamem_unmap(void *, caddr_t, size_t);
paddr_t	iommu_dvmamem_mmap(void *, bus_dma_segment_t *, int, off_t, int, int);

void	iommu_enter(struct astro_softc *, bus_addr_t, paddr_t, vaddr_t, int);
void	iommu_remove(struct astro_softc *, bus_addr_t);

struct iommu_map_state *iommu_iomap_create(int);
void	iommu_iomap_destroy(struct iommu_map_state *);
int	iommu_iomap_insert_page(struct iommu_map_state *, vaddr_t, paddr_t);
bus_addr_t iommu_iomap_translate(struct iommu_map_state *, paddr_t);
void	iommu_iomap_clear_pages(struct iommu_map_state *);

const struct hppa_bus_dma_tag astro_dmat = {
	NULL,
	iommu_dvmamap_create, iommu_dvmamap_destroy,
	iommu_dvmamap_load, iommu_dvmamap_load_mbuf,
	iommu_dvmamap_load_uio, iommu_dvmamap_load_raw,
	iommu_dvmamap_unload, iommu_dvmamap_sync,

	iommu_dvmamem_alloc, iommu_dvmamem_free, iommu_dvmamem_map,
	iommu_dvmamem_unmap, iommu_dvmamem_mmap
};

int
astro_match(struct device *parent, void *cfdata, void *aux)   
{
	struct confargs *ca = aux;

	/* Astro is a U-Turn variant. */
	if (ca->ca_type.iodc_type != HPPA_TYPE_IOA ||
	    ca->ca_type.iodc_sv_model != HPPA_IOA_UTURN)
		return 0;

	if (ca->ca_type.iodc_model == 0x58 &&
	    ca->ca_type.iodc_revision >= 0x20)
		return 1;

	return 0;
}

void
astro_attach(struct device *parent, struct device *self, void *aux)
{
	struct confargs *ca = aux, nca;
	struct astro_softc *sc = (struct astro_softc *)self;
	volatile struct astro_regs *r;
	bus_space_handle_t ioh;
	u_int32_t rid, ioc_ctrl;
	psize_t size;
	vaddr_t va;
	paddr_t pa;
	struct vm_page *m;
	struct pglist mlist;
	int iova_bits;

	sc->sc_dmat = ca->ca_dmatag;
	if (bus_space_map(ca->ca_iot, ca->ca_hpa, sizeof(struct astro_regs),
	    0, &ioh)) {
		printf(": can't map IO space\n");
		return;
	}
	sc->sc_regs = r = (struct astro_regs *)ca->ca_hpa;

	rid = letoh32(r->rid);
	printf(": Astro rev %d.%d\n", (rid & 7) + 1, (rid >> 3) & 3);

	ioc_ctrl = letoh32(r->ioc_ctrl);
	ioc_ctrl &= ~ASTRO_IOC_CTRL_CE;
	ioc_ctrl &= ~ASTRO_IOC_CTRL_RM;
	ioc_ctrl &= ~ASTRO_IOC_CTRL_NC;
	r->ioc_ctrl = htole32(ioc_ctrl);

	/*
	 * Setup the iommu.
	 */

	/* XXX This gives us 256MB of iova space. */
	iova_bits = 28;

	r->tlb_ibase = htole32(0);
	r->tlb_imask = htole32(0xffffffff << iova_bits);

	/* Page size is 4K. */
	r->tlb_tcnfg = htole32(0);

	/* Flush TLB. */
	r->tlb_pcom = htole32(31);

	/*
	 * Allocate memory for I/O pagetables.  They need to be physically
	 * contiguous.
	 */

	size = (1 << (iova_bits - PAGE_SHIFT)) * sizeof(u_int64_t);
	TAILQ_INIT(&mlist);
	if (uvm_pglistalloc(size, 0, -1, PAGE_SIZE, 0, &mlist,
	    1, UVM_PLA_NOWAIT) != 0)
		panic("astrottach: no memory");

	va = (vaddr_t)km_alloc(size, &kv_any, &kp_none, &kd_nowait);
	if (va == 0)
		panic("astroattach: no memory");
	sc->sc_pdir = (u_int64_t *)va;

	m = TAILQ_FIRST(&mlist);
	r->tlb_pdir_base = htole64(VM_PAGE_TO_PHYS(m));

	/* Map the pages. */
	for (; m != NULL; m = TAILQ_NEXT(m, pageq)) {
		pa = VM_PAGE_TO_PHYS(m);
		pmap_enter(pmap_kernel(), va, pa,
		    PROT_READ | PROT_WRITE, PMAP_WIRED);
		va += PAGE_SIZE;
	}
	pmap_update(pmap_kernel());
	memset(sc->sc_pdir, 0, size);

	/*
	 * The PDC might have set up some devices to do DMA.  It will do
	 * this for the onboard USB controller if an USB keyboard is used
	 * for console input.  In that case, bad things will happen if we
	 * enable iova space.  So reset the PDC devices before we do that.
	 * Don't do this if we're using a serial console though, since it
	 * will stop working if we do.  This is fine since the serial port
	 * doesn't do DMA.
	 */
	if (PAGE0->mem_cons.pz_class != PCL_DUPLEX)
		pdc_call((iodcio_t)pdc, 0, PDC_IO, PDC_IO_RESET_DEVICES);

	/* Enable iova space. */
	r->tlb_ibase = htole32(1);

        /*
         * Now all the hardware's working we need to allocate a dvma map.
         */
	snprintf(sc->sc_dvmamapname, sizeof(sc->sc_dvmamapname),
	    "%s_dvma", sc->sc_dv.dv_xname);
	sc->sc_dvmamap = extent_create(sc->sc_dvmamapname, 0, (1 << iova_bits),
	    M_DEVBUF, NULL, 0, EX_NOWAIT | EX_NOCOALESCE);
	KASSERT(sc->sc_dvmamap);
	mtx_init(&sc->sc_dvmamtx, IPL_HIGH);

	sc->sc_dmatag = astro_dmat;
	sc->sc_dmatag._cookie = sc;

	nca = *ca;	/* clone from us */
	nca.ca_hpamask = HPPA_IOBEGIN;
	nca.ca_dmatag = &sc->sc_dmatag;
	pdc_scanbus(self, &nca, MAXMODBUS, 0, 0);
}

int
iommu_dvmamap_create(void *v, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, int flags, bus_dmamap_t *dmamap)
{
	struct astro_softc *sc = v;
	bus_dmamap_t map;
	struct iommu_map_state *ims;
	int error;

	error = bus_dmamap_create(sc->sc_dmat, size, nsegments, maxsegsz,
	    boundary, flags, &map);
	if (error)
		return (error);

	ims = iommu_iomap_create(atop(round_page(size)));
	if (ims == NULL) {
		bus_dmamap_destroy(sc->sc_dmat, map);
		return (ENOMEM);
	}

	ims->ims_sc = sc;
	map->_dm_cookie = ims;
	*dmamap = map;

	return (0);
}

void
iommu_dvmamap_destroy(void *v, bus_dmamap_t map)
{
	struct astro_softc *sc = v;

	/*
	 * The specification (man page) requires a loaded
	 * map to be unloaded before it is destroyed.
	 */
	if (map->dm_nsegs)
		iommu_dvmamap_unload(sc, map);

        if (map->_dm_cookie)
                iommu_iomap_destroy(map->_dm_cookie);
	map->_dm_cookie = NULL;

	bus_dmamap_destroy(sc->sc_dmat, map);
}

int
iommu_iomap_load_map(struct astro_softc *sc, bus_dmamap_t map, int flags)
{
	struct iommu_map_state *ims = map->_dm_cookie;
	struct iommu_page_map *ipm = &ims->ims_map;
	struct iommu_page_entry *e;
	int err, seg;
	paddr_t pa, paend;
	vaddr_t va;
	bus_size_t sgsize;
	bus_size_t align, boundary;
	u_long dvmaddr;
	bus_addr_t dva;
	int i;

	/* XXX */
	boundary = map->_dm_boundary;
	align = PAGE_SIZE;

	iommu_iomap_clear_pages(ims);

	for (seg = 0; seg < map->dm_nsegs; seg++) {
		struct hppa_bus_dma_segment *ds = &map->dm_segs[seg];

		paend = round_page(ds->ds_addr + ds->ds_len);
		for (pa = trunc_page(ds->ds_addr), va = trunc_page(ds->_ds_va);
		     pa < paend; pa += PAGE_SIZE, va += PAGE_SIZE) {
			err = iommu_iomap_insert_page(ims, va, pa);
			if (err) {
				printf("iomap insert error: %d for "
				    "va 0x%lx pa 0x%lx\n", err, va, pa);
				bus_dmamap_unload(sc->sc_dmat, map);
				iommu_iomap_clear_pages(ims);
			}
		}
	}

	sgsize = ims->ims_map.ipm_pagecnt * PAGE_SIZE;
	mtx_enter(&sc->sc_dvmamtx);
	err = extent_alloc_with_descr(sc->sc_dvmamap, sgsize, align, 0,
	    boundary, EX_NOWAIT | EX_BOUNDZERO, &ims->ims_er, &dvmaddr);
	mtx_leave(&sc->sc_dvmamtx);
	if (err)
		return (err);

	ims->ims_dvmastart = dvmaddr;
	ims->ims_dvmasize = sgsize;

	dva = dvmaddr;
	for (i = 0, e = ipm->ipm_map; i < ipm->ipm_pagecnt; ++i, ++e) {
		e->ipe_dva = dva;
		iommu_enter(sc, e->ipe_dva, e->ipe_pa, e->ipe_va, flags);
		dva += PAGE_SIZE;
	}

	for (seg = 0; seg < map->dm_nsegs; seg++) {
		struct hppa_bus_dma_segment *ds = &map->dm_segs[seg];
		ds->ds_addr = iommu_iomap_translate(ims, ds->ds_addr);
	}

	return (0);
}

int
iommu_dvmamap_load(void *v, bus_dmamap_t map, void *addr, bus_size_t size,
    struct proc *p, int flags)
{
	struct astro_softc *sc = v;
	int err;

	err = bus_dmamap_load(sc->sc_dmat, map, addr, size, p, flags);
	if (err)
		return (err);

	return iommu_iomap_load_map(sc, map, flags);
}

int
iommu_dvmamap_load_mbuf(void *v, bus_dmamap_t map, struct mbuf *m, int flags)
{
	struct astro_softc *sc = v;
	int err;

	err = bus_dmamap_load_mbuf(sc->sc_dmat, map, m, flags);
	if (err)
		return (err);

	return iommu_iomap_load_map(sc, map, flags);
}

int
iommu_dvmamap_load_uio(void *v, bus_dmamap_t map, struct uio *uio, int flags)
{
	struct astro_softc *sc = v;

	printf("load_uio\n");

	return (bus_dmamap_load_uio(sc->sc_dmat, map, uio, flags));
}

int
iommu_dvmamap_load_raw(void *v, bus_dmamap_t map, bus_dma_segment_t *segs,
    int nsegs, bus_size_t size, int flags)
{
	struct astro_softc *sc = v;

	printf("load_raw\n");

	return (bus_dmamap_load_raw(sc->sc_dmat, map, segs, nsegs, size, flags));
}

void
iommu_dvmamap_unload(void *v, bus_dmamap_t map)
{
	struct astro_softc *sc = v;
	struct iommu_map_state *ims = map->_dm_cookie;
	struct iommu_page_map *ipm = &ims->ims_map;
	struct iommu_page_entry *e;
	int err, i;

	/* Remove the IOMMU entries. */
	for (i = 0, e = ipm->ipm_map; i < ipm->ipm_pagecnt; ++i, ++e)
		iommu_remove(sc, e->ipe_dva);

	/* Clear the iomap. */
	iommu_iomap_clear_pages(ims);

	bus_dmamap_unload(sc->sc_dmat, map);

	mtx_enter(&sc->sc_dvmamtx);
	err = extent_free(sc->sc_dvmamap, ims->ims_dvmastart,
	    ims->ims_dvmasize, EX_NOWAIT);
	ims->ims_dvmastart = 0;
	ims->ims_dvmasize = 0;
	mtx_leave(&sc->sc_dvmamtx);
	if (err)
		printf("warning: %ld of DVMA space lost\n", ims->ims_dvmasize);
}

void
iommu_dvmamap_sync(void *v, bus_dmamap_t map, bus_addr_t off,
    bus_size_t len, int ops)
{
	/* Nothing to do; DMA is cache-coherent. */
}

int
iommu_dvmamem_alloc(void *v, bus_size_t size, bus_size_t alignment,
    bus_size_t boundary, bus_dma_segment_t *segs,
    int nsegs, int *rsegs, int flags)
{
	struct astro_softc *sc = v;

	return (bus_dmamem_alloc(sc->sc_dmat, size, alignment, boundary,
	    segs, nsegs, rsegs, flags));
}

void
iommu_dvmamem_free(void *v, bus_dma_segment_t *segs, int nsegs)
{
	struct astro_softc *sc = v;

	bus_dmamem_free(sc->sc_dmat, segs, nsegs);
}

int
iommu_dvmamem_map(void *v, bus_dma_segment_t *segs, int nsegs, size_t size,
    caddr_t *kvap, int flags)
{
	struct astro_softc *sc = v;

	return (bus_dmamem_map(sc->sc_dmat, segs, nsegs, size, kvap, flags));
}

void
iommu_dvmamem_unmap(void *v, caddr_t kva, size_t size)
{
	struct astro_softc *sc = v;

	bus_dmamem_unmap(sc->sc_dmat, kva, size);
}

paddr_t
iommu_dvmamem_mmap(void *v, bus_dma_segment_t *segs, int nsegs, off_t off,
    int prot, int flags)
{
	struct astro_softc *sc = v;

	return (bus_dmamem_mmap(sc->sc_dmat, segs, nsegs, off, prot, flags));
}

/*
 * Utility function used by splay tree to order page entries by pa.
 */
static inline int
iomap_compare(struct iommu_page_entry *a, struct iommu_page_entry *b)
{
	return ((a->ipe_pa > b->ipe_pa) ? 1 :
		(a->ipe_pa < b->ipe_pa) ? -1 : 0);
}

SPLAY_PROTOTYPE(iommu_page_tree, iommu_page_entry, ipe_node, iomap_compare);

SPLAY_GENERATE(iommu_page_tree, iommu_page_entry, ipe_node, iomap_compare);

/*
 * Create a new iomap.
 */
struct iommu_map_state *
iommu_iomap_create(int n)
{
	struct iommu_map_state *ims;

	/* Safety for heavily fragmented data, such as mbufs */
	n += 4;
	if (n < 16)
		n = 16;

	ims = malloc(sizeof(*ims) + (n - 1) * sizeof(ims->ims_map.ipm_map[0]),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ims == NULL)
		return (NULL);

	/* Initialize the map. */
	ims->ims_map.ipm_maxpage = n;
	SPLAY_INIT(&ims->ims_map.ipm_tree);

	return (ims);
}

/*
 * Destroy an iomap.
 */
void
iommu_iomap_destroy(struct iommu_map_state *ims)
{
#ifdef DIAGNOSTIC
	if (ims->ims_map.ipm_pagecnt > 0)
		printf("iommu_iomap_destroy: %d page entries in use\n",
		    ims->ims_map.ipm_pagecnt);
#endif

	free(ims, M_DEVBUF, 0);
}

/*
 * Insert a pa entry in the iomap.
 */
int
iommu_iomap_insert_page(struct iommu_map_state *ims, vaddr_t va, paddr_t pa)
{
	struct iommu_page_map *ipm = &ims->ims_map;
	struct iommu_page_entry *e;

	if (ipm->ipm_pagecnt >= ipm->ipm_maxpage) {
		struct iommu_page_entry ipe;

		ipe.ipe_pa = pa;
		if (SPLAY_FIND(iommu_page_tree, &ipm->ipm_tree, &ipe))
			return (0);

		return (ENOMEM);
	}

	e = &ipm->ipm_map[ipm->ipm_pagecnt];

	e->ipe_pa = pa;
	e->ipe_va = va;
	e->ipe_dva = 0;

	e = SPLAY_INSERT(iommu_page_tree, &ipm->ipm_tree, e);

	/* Duplicates are okay, but only count them once. */
	if (e)
		return (0);

	++ipm->ipm_pagecnt;

	return (0);
}

/*
 * Translate a physical address (pa) into a DVMA address.
 */
bus_addr_t
iommu_iomap_translate(struct iommu_map_state *ims, paddr_t pa)
{
	struct iommu_page_map *ipm = &ims->ims_map;
	struct iommu_page_entry *e;
	struct iommu_page_entry pe;
	paddr_t offset = pa & PAGE_MASK;

	pe.ipe_pa = trunc_page(pa);

	e = SPLAY_FIND(iommu_page_tree, &ipm->ipm_tree, &pe);

	if (e == NULL) {
		panic("couldn't find pa %lx", pa);
		return 0;
	}

	return (e->ipe_dva | offset);
}

/*
 * Clear the iomap table and tree.
 */
void
iommu_iomap_clear_pages(struct iommu_map_state *ims)
{
        ims->ims_map.ipm_pagecnt = 0;
        SPLAY_INIT(&ims->ims_map.ipm_tree);
}

/*
 * Add an entry to the IOMMU table.
 */
void
iommu_enter(struct astro_softc *sc, bus_addr_t dva, paddr_t pa, vaddr_t va,
    int flags)
{
	volatile u_int64_t *tte_ptr = &sc->sc_pdir[dva >> PAGE_SHIFT];
	u_int64_t tte;
	u_int32_t ci;

#ifdef DEBUG
	printf("iommu_enter dva %lx, pa %lx, va %lx\n", dva, pa, va);
#endif

#ifdef DIAGNOSTIC
	tte = letoh64(*tte_ptr);

	if (tte & IOTTE_V) {
		printf("Overwriting valid tte entry (dva %lx pa %lx "
		    "&tte %p tte %llx)\n", dva, pa, tte_ptr, tte);
		extent_print(sc->sc_dvmamap);
		panic("IOMMU overwrite");
	}
#endif

	mtsp(HPPA_SID_KERNEL, 1);
	__asm volatile("lci 0(%%sr1, %1), %0" : "=r" (ci) : "r" (va));

	tte = (pa & IOTTE_PAMASK) | ((ci >> 12) & IOTTE_CI);
	tte |= IOTTE_V;

	*tte_ptr = htole64(tte);
	__asm volatile("fdc 0(%%sr1, %0)\n\tsync" : : "r" (tte_ptr));
}

/*
 * Remove an entry from the IOMMU table.
 */
void
iommu_remove(struct astro_softc *sc, bus_addr_t dva)
{
	volatile struct astro_regs *r = sc->sc_regs;
	u_int64_t *tte_ptr = &sc->sc_pdir[dva >> PAGE_SHIFT];
	u_int64_t tte;

#ifdef DIAGNOSTIC
	if (dva != trunc_page(dva)) {
		printf("iommu_remove: unaligned dva: %lx\n", dva);
		dva = trunc_page(dva);
	}
#endif

	tte = letoh64(*tte_ptr);

#ifdef DIAGNOSTIC
	if ((tte & IOTTE_V) == 0) {
		printf("Removing invalid tte entry (dva %lx &tte %p "
		    "tte %llx)\n", dva, tte_ptr, tte);
		extent_print(sc->sc_dvmamap);
		panic("IOMMU remove overwrite");
	}
#endif

	*tte_ptr = htole64(tte & ~IOTTE_V);

	/* Flush IOMMU. */
	r->tlb_pcom = htole32(dva | PAGE_SHIFT);
}
