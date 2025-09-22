/*	$OpenBSD: apldart.c,v 1.21 2024/05/13 01:15:50 jsg Exp $	*/
/*
 * Copyright (c) 2021 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <uvm/uvm_extern.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

/*
 * This driver largely ignores stream IDs and simply uses a single
 * translation table for all the devices that it serves.  This is good
 * enough for the PCIe host bridge that serves the on-board devices on
 * the current generation Apple Silicon Macs as these only have a
 * single PCIe device behind each DART.
 */

#define DART_PARAMS2			0x0004
#define  DART_PARAMS2_BYPASS_SUPPORT		(1 << 0)

#define DART_T8020_TLB_CMD		0x0020
#define  DART_T8020_TLB_CMD_FLUSH		(1 << 20)
#define  DART_T8020_TLB_CMD_BUSY		(1 << 2)
#define DART_T8020_TLB_SIDMASK		0x0034
#define DART_T8020_ERROR		0x0040
#define DART_T8020_ERROR_ADDR_LO	0x0050
#define DART_T8020_ERROR_ADDR_HI	0x0054
#define DART_T8020_CONFIG		0x0060
#define  DART_T8020_CONFIG_LOCK			(1 << 15)
#define DART_T8020_SID_ENABLE		0x00fc
#define DART_T8020_TCR_BASE		0x0100
#define  DART_T8020_TCR_TRANSLATE_ENABLE	(1 << 7)
#define  DART_T8020_TCR_BYPASS_DART		(1 << 8)
#define  DART_T8020_TCR_BYPASS_DAPF		(1 << 12)
#define DART_T8020_TTBR_BASE		0x0200
#define  DART_T8020_TTBR_VALID			(1U << 31)

#define DART_T8110_PARAMS3		0x0008
#define  DART_T8110_PARAMS3_REV_MIN(x)		(((x) >> 0) & 0xff)
#define  DART_T8110_PARAMS3_REV_MAJ(x)		(((x) >> 8) & 0xff)
#define  DART_T8110_PARAMS3_VA_WIDTH(x)		(((x) >> 16) & 0x3f)
#define DART_T8110_PARAMS4		0x000c
#define  DART_T8110_PARAMS4_NSID_MASK		(0x1ff << 0)
#define DART_T8110_TLB_CMD		0x0080
#define  DART_T8110_TLB_CMD_BUSY		(1U << 31)
#define  DART_T8110_TLB_CMD_FLUSH_ALL		(0 << 8)
#define  DART_T8110_TLB_CMD_FLUSH_SID		(1 << 8)
#define DART_T8110_ERROR		0x0100
#define DART_T8110_ERROR_MASK		0x0104
#define DART_T8110_ERROR_ADDR_LO	0x0170
#define DART_T8110_ERROR_ADDR_HI	0x0174
#define DART_T8110_PROTECT		0x0200
#define  DART_T8110_PROTECT_TTBR_TCR		(1 << 0)
#define DART_T8110_SID_ENABLE_BASE	0x0c00
#define DART_T8110_TCR_BASE		0x1000
#define  DART_T8110_TCR_BYPASS_DAPF		(1 << 2)
#define  DART_T8110_TCR_BYPASS_DART		(1 << 1)
#define  DART_T8110_TCR_TRANSLATE_ENABLE	(1 << 0)
#define DART_T8110_TTBR_BASE		0x1400
#define  DART_T8110_TTBR_VALID			(1 << 0)

#define DART_PAGE_SIZE		16384
#define DART_PAGE_MASK		(DART_PAGE_SIZE - 1)

#define DART_SID_ENABLE(sc, idx) \
    ((sc)->sc_sid_enable_base + 4 * (idx))
#define DART_TCR(sc, sid)	((sc)->sc_tcr_base + 4 * (sid))
#define DART_TTBR(sc, sid, idx)	\
    ((sc)->sc_ttbr_base + 4 * (sc)->sc_nttbr * (sid) + 4 * (idx))
#define  DART_TTBR_SHIFT	12

#define DART_ALL_STREAMS(sc)	((1U << (sc)->sc_nsid) - 1)

/*
 * Some hardware (e.g. bge(4)) will always use (aligned) 64-bit memory
 * access.  To make sure this doesn't fault, round the subpage limits
 * down and up accordingly.
 */
#define DART_OFFSET_MASK	7

#define DART_L1_TABLE		0x3
#define DART_L2_INVAL		0
#define DART_L2_VALID		(1 << 0)
#define DART_L2_FULL_PAGE	(1 << 1)
#define DART_L2_START(addr)	((((addr) & DART_PAGE_MASK) >> 2) << 52)
#define DART_L2_END(addr)	((((addr) & DART_PAGE_MASK) >> 2) << 40)

static inline paddr_t
apldart_round_page(paddr_t pa)
{
	return ((pa + DART_PAGE_MASK) & ~DART_PAGE_MASK);
}

static inline paddr_t
apldart_trunc_page(paddr_t pa)
{
	return (pa & ~DART_PAGE_MASK);
}

static inline psize_t
apldart_round_offset(psize_t off)
{
	return ((off + DART_OFFSET_MASK) & ~DART_OFFSET_MASK);
}

static inline psize_t
apldart_trunc_offset(psize_t off)
{
	return (off & ~DART_OFFSET_MASK);
}

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct apldart_stream {
	struct apldart_softc	*as_sc;
	int			as_sid;

	struct extent		*as_dvamap;
	struct mutex		as_dvamap_mtx;
	struct apldart_dmamem	*as_l1;
	struct apldart_dmamem	**as_l2;

	struct machine_bus_dma_tag as_dmat;
};

struct apldart_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;
	int			sc_node;

	int			sc_ias;
	int			sc_nsid;
	int			sc_nttbr;
	int			sc_shift;
	bus_addr_t		sc_sid_enable_base;
	bus_addr_t		sc_tcr_base;
	uint32_t		sc_tcr_translate_enable;
	uint32_t		sc_tcr_bypass;
	bus_addr_t		sc_ttbr_base;
	uint32_t		sc_ttbr_valid;
	void			(*sc_flush_tlb)(struct apldart_softc *, int);

	bus_addr_t		sc_dvabase;
	bus_addr_t		sc_dvaend;
	bus_addr_t		sc_dvamask;

	struct apldart_stream	**sc_as;
	struct iommu_device	sc_id;

	int			sc_locked;
	int			sc_translating;
	int			sc_do_suspend;
};

struct apldart_map_state {
	struct extent_region	ams_er;
	bus_addr_t		ams_dva;
	bus_size_t		ams_len;
};

struct apldart_dmamem {
	bus_dmamap_t		adm_map;
	bus_dma_segment_t	adm_seg;
	size_t			adm_size;
	caddr_t			adm_kva;
};

#define APLDART_DMA_MAP(_adm)	((_adm)->adm_map)
#define APLDART_DMA_LEN(_adm)	((_adm)->adm_size)
#define APLDART_DMA_DVA(_adm)	((_adm)->adm_map->dm_segs[0].ds_addr)
#define APLDART_DMA_KVA(_adm)	((void *)(_adm)->adm_kva)

struct apldart_dmamem *apldart_dmamem_alloc(bus_dma_tag_t, bus_size_t,
	    bus_size_t);
void	apldart_dmamem_free(bus_dma_tag_t, struct apldart_dmamem *);

int	apldart_match(struct device *, void *, void *);
void	apldart_attach(struct device *, struct device *, void *);
int	apldart_activate(struct device *, int);

const struct cfattach apldart_ca = {
	sizeof (struct apldart_softc), apldart_match, apldart_attach, NULL,
	apldart_activate
};

struct cfdriver apldart_cd = {
	NULL, "apldart", DV_DULL
};

bus_dma_tag_t apldart_map(void *, uint32_t *, bus_dma_tag_t);
void	apldart_reserve(void *, uint32_t *, bus_addr_t, bus_size_t);
int	apldart_t8020_intr(void *);
int	apldart_t8110_intr(void *);

void	apldart_t8020_flush_tlb(struct apldart_softc *, int);
void	apldart_t8110_flush_tlb(struct apldart_softc *, int);
int	apldart_load_map(struct apldart_stream *, bus_dmamap_t, int);
void	apldart_unload_map(struct apldart_stream *, bus_dmamap_t);

int	apldart_dmamap_create(bus_dma_tag_t, bus_size_t, int, bus_size_t,
	    bus_size_t boundary, int, bus_dmamap_t *);
void	apldart_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);
int	apldart_dmamap_load(bus_dma_tag_t, bus_dmamap_t, void *,
	    bus_size_t, struct proc *, int);
int	apldart_dmamap_load_mbuf(bus_dma_tag_t, bus_dmamap_t,
	    struct mbuf *, int);
int	apldart_dmamap_load_uio(bus_dma_tag_t, bus_dmamap_t,
	    struct uio *, int);
int	apldart_dmamap_load_raw(bus_dma_tag_t, bus_dmamap_t,
	    bus_dma_segment_t *, int, bus_size_t, int);
void	apldart_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);

int
apldart_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,t6000-dart") ||
	    OF_is_compatible(faa->fa_node, "apple,t8103-dart") ||
	    OF_is_compatible(faa->fa_node, "apple,t8110-dart");
}

void
apldart_attach(struct device *parent, struct device *self, void *aux)
{
	struct apldart_softc *sc = (struct apldart_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint64_t dva_range[2];
	uint32_t config, maj, min, params2, params3, params4, tcr, ttbr;
	int sid, idx;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_dmat = faa->fa_dmat;
	sc->sc_node = faa->fa_node;

	power_domain_enable(sc->sc_node);

	if (OF_is_compatible(sc->sc_node, "apple,t8110-dart")) {
		params3 = HREAD4(sc, DART_T8110_PARAMS3);
		params4 = HREAD4(sc, DART_T8110_PARAMS4);
		sc->sc_ias = DART_T8110_PARAMS3_VA_WIDTH(params3);
		sc->sc_nsid = params4 & DART_T8110_PARAMS4_NSID_MASK;
		sc->sc_nttbr = 1;
		sc->sc_sid_enable_base = DART_T8110_SID_ENABLE_BASE;
		sc->sc_tcr_base = DART_T8110_TCR_BASE;
		sc->sc_tcr_translate_enable = DART_T8110_TCR_TRANSLATE_ENABLE;
		sc->sc_tcr_bypass =
		    DART_T8110_TCR_BYPASS_DAPF | DART_T8110_TCR_BYPASS_DART;
		sc->sc_ttbr_base = DART_T8110_TTBR_BASE;
		sc->sc_ttbr_valid = DART_T8110_TTBR_VALID;
		sc->sc_flush_tlb = apldart_t8110_flush_tlb;
		maj = DART_T8110_PARAMS3_REV_MAJ(params3);
		min = DART_T8110_PARAMS3_REV_MIN(params3);
	} else {
		sc->sc_ias = 32;
		sc->sc_nsid = 16;
		sc->sc_nttbr = 4;
		sc->sc_sid_enable_base = DART_T8020_SID_ENABLE;
		sc->sc_tcr_base = DART_T8020_TCR_BASE;
		sc->sc_tcr_translate_enable = DART_T8020_TCR_TRANSLATE_ENABLE;
		sc->sc_tcr_bypass =
		    DART_T8020_TCR_BYPASS_DAPF | DART_T8020_TCR_BYPASS_DART;
		sc->sc_ttbr_base = DART_T8020_TTBR_BASE;
		sc->sc_ttbr_valid = DART_T8020_TTBR_VALID;
		sc->sc_flush_tlb = apldart_t8020_flush_tlb;
		maj = min = 0;
	}

	if (OF_is_compatible(sc->sc_node, "apple,t6000-dart") ||
	    OF_is_compatible(sc->sc_node, "apple,t8110-dart"))
		sc->sc_shift = 4;

	/* Skip locked DARTs for now. */
	if (OF_is_compatible(sc->sc_node, "apple,t8110-dart")) {
		config = HREAD4(sc, DART_T8110_PROTECT);
		if (config & DART_T8110_PROTECT_TTBR_TCR)
			sc->sc_locked = 1;
	} else {
		config = HREAD4(sc, DART_T8020_CONFIG);
		if (config & DART_T8020_CONFIG_LOCK)
			sc->sc_locked = 1;
	}

	if (maj != 0 || min != 0)
		printf(" rev %d.%d", maj, min);

	printf(": %d bits", sc->sc_ias);

	/*
	 * Anything over 36 bits requires 4-level page tables which we
	 * don't implement yet.  So limit to 36 bits.
	 */
	if (sc->sc_ias > 36)
		sc->sc_ias = 36;
	sc->sc_dvamask = (1ULL << sc->sc_ias) - 1;

	/*
	 * Resetting the DART used for the display controller will
	 * kill the framebuffer.  This should be the only DART that
	 * has translation enabled and a valid translation table
	 * installed.  Skip this DART for now.
	 */
	for (sid = 0; sid < sc->sc_nsid; sid++) {
		tcr = HREAD4(sc, DART_TCR(sc, sid));
		if ((tcr & sc->sc_tcr_translate_enable) == 0)
			continue;

		for (idx = 0; idx < sc->sc_nttbr; idx++) {
			ttbr = HREAD4(sc, DART_TTBR(sc, sid, idx));
			if (ttbr & sc->sc_ttbr_valid)
				sc->sc_translating = 1;
		}
	}

	/*
	 * If we have full control over this DART, do suspend it.
	 */
	sc->sc_do_suspend = !sc->sc_locked && !sc->sc_translating;

	/*
	 * Use bypass mode if supported.  This avoids an issue with
	 * the USB3 controllers which need mappings entered into two
	 * IOMMUs, which is somewhat difficult to implement with our
	 * current kernel interfaces.
	 */
	params2 = HREAD4(sc, DART_PARAMS2);
	if ((params2 & DART_PARAMS2_BYPASS_SUPPORT) &&
	    !sc->sc_locked && !sc->sc_translating) {
		for (sid = 0; sid < sc->sc_nsid; sid++)
			HWRITE4(sc, DART_TCR(sc, sid), sc->sc_tcr_bypass);
		printf(", bypass\n");
		return;
	}

	if (sc->sc_locked)
		printf(", locked\n");
	else if (sc->sc_translating)
		printf(", translating\n");
	else
		printf("\n");

	if (OF_getpropint64array(sc->sc_node, "apple,dma-range",
	    dva_range, sizeof(dva_range)) == sizeof(dva_range)) {
		sc->sc_dvabase = dva_range[0];
		sc->sc_dvaend = dva_range[0] + dva_range[1] - 1;
	} else {
		/*
		 * Restrict ourselves to 32-bit addresses to cater for
		 * devices that don't do 64-bit DMA.  Skip the first
		 * page to help catching bugs where a device is doing
		 * DMA to/from address zero because we didn't properly
		 * set up the DMA transfer.  Skip the last page to
		 * avoid using the address reserved for MSIs.
		 */
		sc->sc_dvabase = DART_PAGE_SIZE;
		sc->sc_dvaend = 0xffffffff - DART_PAGE_SIZE;
	}

	if (!sc->sc_locked && !sc->sc_translating) {
		/* Disable translations. */
		for (sid = 0; sid < sc->sc_nsid; sid++)
			HWRITE4(sc, DART_TCR(sc, sid), 0);

		/* Remove page tables. */
		for (sid = 0; sid < sc->sc_nsid; sid++) {
			for (idx = 0; idx < sc->sc_nttbr; idx++)
				HWRITE4(sc, DART_TTBR(sc, sid, idx), 0);
		}
		sc->sc_flush_tlb(sc, -1);
	}

	if (OF_is_compatible(sc->sc_node, "apple,t8110-dart")) {
		HWRITE4(sc, DART_T8110_ERROR, HREAD4(sc, DART_T8110_ERROR));
		HWRITE4(sc, DART_T8110_ERROR_MASK, 0);
		fdt_intr_establish(faa->fa_node, IPL_NET, apldart_t8110_intr,
		    sc, sc->sc_dev.dv_xname);
	} else {
		HWRITE4(sc, DART_T8020_ERROR, HREAD4(sc, DART_T8020_ERROR));
		fdt_intr_establish(faa->fa_node, IPL_NET, apldart_t8020_intr,
		    sc, sc->sc_dev.dv_xname);
	}

	sc->sc_as = mallocarray(sc->sc_nsid, sizeof(*sc->sc_as),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	sc->sc_id.id_node = faa->fa_node;
	sc->sc_id.id_cookie = sc;
	sc->sc_id.id_map = apldart_map;
	sc->sc_id.id_reserve = apldart_reserve;
	iommu_device_register(&sc->sc_id);
}

void
apldart_suspend(struct apldart_softc *sc)
{
	if (!sc->sc_do_suspend)
		return;

	power_domain_disable(sc->sc_node);
}

void
apldart_resume(struct apldart_softc *sc)
{
	paddr_t pa;
	int ntte, nl1, nl2;
	uint32_t params2;
	uint32_t mask;
	int sid, idx;

	if (!sc->sc_do_suspend)
		return;

	power_domain_enable(sc->sc_node);

	params2 = HREAD4(sc, DART_PARAMS2);
	if (params2 & DART_PARAMS2_BYPASS_SUPPORT) {
		for (sid = 0; sid < sc->sc_nsid; sid++)
			HWRITE4(sc, DART_TCR(sc, sid), sc->sc_tcr_bypass);
		return;
	}

	ntte = howmany((sc->sc_dvaend & sc->sc_dvamask), DART_PAGE_SIZE);
	nl2 = howmany(ntte, DART_PAGE_SIZE / sizeof(uint64_t));
	nl1 = howmany(nl2, DART_PAGE_SIZE / sizeof(uint64_t));

	/* Install page tables. */
	for (sid = 0; sid < sc->sc_nsid; sid++) {
		if (sc->sc_as[sid] == NULL)
			continue;
		pa = APLDART_DMA_DVA(sc->sc_as[sid]->as_l1);
		for (idx = 0; idx < nl1; idx++) {
			HWRITE4(sc, DART_TTBR(sc, sid, idx),
			    (pa >> DART_TTBR_SHIFT) | sc->sc_ttbr_valid);
			pa += DART_PAGE_SIZE;
		}
	}
	sc->sc_flush_tlb(sc, -1);

	/* Enable all active streams. */
	for (sid = 0; sid < sc->sc_nsid; sid++) {
		if (sc->sc_as[sid] == NULL)
			continue;
		mask = HREAD4(sc, DART_SID_ENABLE(sc, sid / 32));
		mask |= (1U << (sid % 32));
		HWRITE4(sc, DART_SID_ENABLE(sc, sid / 32), mask);
	}

	/* Enable translations. */
	for (sid = 0; sid < sc->sc_nsid; sid++) {
		if (sc->sc_as[sid] == NULL)
			continue;
		HWRITE4(sc, DART_TCR(sc, sid), sc->sc_tcr_translate_enable);
	}

	if (OF_is_compatible(sc->sc_node, "apple,t8110-dart")) {
		HWRITE4(sc, DART_T8110_ERROR, HREAD4(sc, DART_T8110_ERROR));
		HWRITE4(sc, DART_T8110_ERROR_MASK, 0);
	} else {
		HWRITE4(sc, DART_T8020_ERROR, HREAD4(sc, DART_T8020_ERROR));
	}
}

int
apldart_activate(struct device *self, int act)
{
	struct apldart_softc *sc = (struct apldart_softc *)self;

	switch (act) {
	case DVACT_SUSPEND:
		apldart_suspend(sc);
		break;
	case DVACT_RESUME:
		apldart_resume(sc);
		break;
	}

	return 0;
}

void
apldart_init_locked_stream(struct apldart_stream *as)
{
	struct apldart_softc *sc = as->as_sc;
	uint32_t ttbr;
	vaddr_t startva, endva, va;
	paddr_t pa;
	bus_addr_t dva, dvaend, dvabase;
	volatile uint64_t *l1;
	int nl1, nl2, ntte;
	int idx;

	for (idx = 0; idx < sc->sc_nttbr; idx++) {
		ttbr = HREAD4(sc, DART_TTBR(sc, as->as_sid, idx));
		if ((ttbr & sc->sc_ttbr_valid) == 0)
			break;
	}
	KASSERT(idx > 0);

	nl2 = idx * (DART_PAGE_SIZE / sizeof(uint64_t));
	ntte = nl2 * (DART_PAGE_SIZE / sizeof(uint64_t));

	dvabase = sc->sc_dvabase & ~sc->sc_dvamask;
	dvaend = dvabase + (bus_addr_t)ntte * DART_PAGE_SIZE;
	if (dvaend < sc->sc_dvaend)
		sc->sc_dvaend = dvaend;

	as->as_dvamap = extent_create(sc->sc_dev.dv_xname, 0, ULONG_MAX,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_NOCOALESCE);
	if (sc->sc_dvabase > 0) {
		extent_alloc_region(as->as_dvamap, 0, sc->sc_dvabase,
		    EX_WAITOK);
	}
	if (sc->sc_dvaend < ULONG_MAX) {
		extent_alloc_region(as->as_dvamap, sc->sc_dvaend + 1,
		    ULONG_MAX - sc->sc_dvaend, EX_WAITOK);
	}

	ntte = howmany((sc->sc_dvaend & sc->sc_dvamask), DART_PAGE_SIZE);
	nl2 = howmany(ntte, DART_PAGE_SIZE / sizeof(uint64_t));
	nl1 = howmany(nl2, DART_PAGE_SIZE / sizeof(uint64_t));

	as->as_l2 = mallocarray(nl2, sizeof(*as->as_l2),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	l1 = km_alloc(nl1 * DART_PAGE_SIZE, &kv_any, &kp_none, &kd_waitok);
	KASSERT(l1);

	for (idx = 0; idx < nl1; idx++) {
		startva = (vaddr_t)l1 + idx * DART_PAGE_SIZE;
		endva = startva + DART_PAGE_SIZE;
		ttbr = HREAD4(sc, DART_TTBR(sc, as->as_sid, idx));
		pa = (paddr_t)(ttbr & ~sc->sc_ttbr_valid) << DART_TTBR_SHIFT;
		for (va = startva; va < endva; va += PAGE_SIZE) {
			pmap_kenter_cache(va, pa, PROT_READ | PROT_WRITE,
			    PMAP_CACHE_CI);
			pa += PAGE_SIZE;
		}
	}

	for (idx = 0; idx < nl2; idx++) {
		if (l1[idx] & DART_L1_TABLE) {
			dva = idx * (DART_PAGE_SIZE / sizeof(uint64_t)) *
			    DART_PAGE_SIZE;
			dvaend = dva + DART_PAGE_SIZE * DART_PAGE_SIZE - 1;
			extent_alloc_region(as->as_dvamap, dvabase + dva,
			    dvaend - dva + 1, EX_WAITOK | EX_CONFLICTOK);
		} else {
			as->as_l2[idx] = apldart_dmamem_alloc(sc->sc_dmat,
			    DART_PAGE_SIZE, DART_PAGE_SIZE);
			pa = APLDART_DMA_DVA(as->as_l2[idx]);
			l1[idx] = (pa >> sc->sc_shift) | DART_L1_TABLE;
		}
	}
	sc->sc_flush_tlb(sc, as->as_sid);

	memcpy(&as->as_dmat, sc->sc_dmat, sizeof(*sc->sc_dmat));
	as->as_dmat._cookie = as;
	as->as_dmat._dmamap_create = apldart_dmamap_create;
	as->as_dmat._dmamap_destroy = apldart_dmamap_destroy;
	as->as_dmat._dmamap_load = apldart_dmamap_load;
	as->as_dmat._dmamap_load_mbuf = apldart_dmamap_load_mbuf;
	as->as_dmat._dmamap_load_uio = apldart_dmamap_load_uio;
	as->as_dmat._dmamap_load_raw = apldart_dmamap_load_raw;
	as->as_dmat._dmamap_unload = apldart_dmamap_unload;
	as->as_dmat._flags |= BUS_DMA_COHERENT;
}

struct apldart_stream *
apldart_alloc_stream(struct apldart_softc *sc, int sid)
{
	struct apldart_stream *as;
	paddr_t pa;
	volatile uint64_t *l1;
	int idx, ntte, nl1, nl2;
	uint32_t mask;

	as = malloc(sizeof(*as), M_DEVBUF, M_WAITOK | M_ZERO);

	as->as_sc = sc;
	as->as_sid = sid;

	mtx_init(&as->as_dvamap_mtx, IPL_HIGH);

	if (sc->sc_locked || sc->sc_translating) {
		apldart_init_locked_stream(as);
		return as;
	}

	as->as_dvamap = extent_create(sc->sc_dev.dv_xname, 0, ULONG_MAX,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_NOCOALESCE);
	if (sc->sc_dvabase > 0) {
		extent_alloc_region(as->as_dvamap, 0, sc->sc_dvabase,
		    EX_WAITOK);
	}
	if (sc->sc_dvaend < ULONG_MAX) {
		extent_alloc_region(as->as_dvamap, sc->sc_dvaend + 1,
		    ULONG_MAX - sc->sc_dvaend, EX_WAITOK);
	}

	/*
	 * Build translation tables.  We pre-allocate the translation
	 * tables for the entire aperture such that we don't have to
	 * worry about growing them in an mpsafe manner later.
	 */

	ntte = howmany((sc->sc_dvaend & sc->sc_dvamask), DART_PAGE_SIZE);
	nl2 = howmany(ntte, DART_PAGE_SIZE / sizeof(uint64_t));
	nl1 = howmany(nl2, DART_PAGE_SIZE / sizeof(uint64_t));

	as->as_l1 = apldart_dmamem_alloc(sc->sc_dmat,
	    nl1 * DART_PAGE_SIZE, DART_PAGE_SIZE);
	as->as_l2 = mallocarray(nl2, sizeof(*as->as_l2),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	l1 = APLDART_DMA_KVA(as->as_l1);
	for (idx = 0; idx < nl2; idx++) {
		as->as_l2[idx] = apldart_dmamem_alloc(sc->sc_dmat,
		    DART_PAGE_SIZE, DART_PAGE_SIZE);
		pa = APLDART_DMA_DVA(as->as_l2[idx]);
		l1[idx] = (pa >> sc->sc_shift) | DART_L1_TABLE;
	}

	/* Install page tables. */
	pa = APLDART_DMA_DVA(as->as_l1);
	for (idx = 0; idx < nl1; idx++) {
		HWRITE4(sc, DART_TTBR(sc, sid, idx),
		    (pa >> DART_TTBR_SHIFT) | sc->sc_ttbr_valid);
		pa += DART_PAGE_SIZE;
	}
	sc->sc_flush_tlb(sc, sid);

	/* Enable this stream. */
	mask = HREAD4(sc, DART_SID_ENABLE(sc, sid / 32));
	mask |= (1U << (sid % 32));
	HWRITE4(sc, DART_SID_ENABLE(sc, sid / 32), mask);

	/* Enable translations. */
	HWRITE4(sc, DART_TCR(sc, sid), sc->sc_tcr_translate_enable);

	memcpy(&as->as_dmat, sc->sc_dmat, sizeof(*sc->sc_dmat));
	as->as_dmat._cookie = as;
	as->as_dmat._dmamap_create = apldart_dmamap_create;
	as->as_dmat._dmamap_destroy = apldart_dmamap_destroy;
	as->as_dmat._dmamap_load = apldart_dmamap_load;
	as->as_dmat._dmamap_load_mbuf = apldart_dmamap_load_mbuf;
	as->as_dmat._dmamap_load_uio = apldart_dmamap_load_uio;
	as->as_dmat._dmamap_load_raw = apldart_dmamap_load_raw;
	as->as_dmat._dmamap_unload = apldart_dmamap_unload;
	as->as_dmat._flags |= BUS_DMA_COHERENT;

	return as;
}

bus_dma_tag_t
apldart_map(void *cookie, uint32_t *cells, bus_dma_tag_t dmat)
{
	struct apldart_softc *sc = cookie;
	uint32_t sid = cells[0];

	KASSERT(sid < sc->sc_nsid);

	if (sc->sc_as[sid] == NULL)
		sc->sc_as[sid] = apldart_alloc_stream(sc, sid);

	return &sc->sc_as[sid]->as_dmat;
}

void
apldart_reserve(void *cookie, uint32_t *cells, bus_addr_t addr, bus_size_t size)
{
}

int
apldart_t8020_intr(void *arg)
{
	struct apldart_softc *sc = arg;

	panic("%s: error 0x%08x addr 0x%08x%08x\n",
	    sc->sc_dev.dv_xname, HREAD4(sc, DART_T8020_ERROR),
	    HREAD4(sc, DART_T8020_ERROR_ADDR_HI),
	    HREAD4(sc, DART_T8020_ERROR_ADDR_LO));
}

int
apldart_t8110_intr(void *arg)
{
	struct apldart_softc *sc = arg;

	panic("%s: error 0x%08x addr 0x%08x%08x\n",
	    sc->sc_dev.dv_xname, HREAD4(sc, DART_T8110_ERROR),
	    HREAD4(sc, DART_T8110_ERROR_ADDR_HI),
	    HREAD4(sc, DART_T8110_ERROR_ADDR_LO));
}

void
apldart_t8020_flush_tlb(struct apldart_softc *sc, int sid)
{
	uint32_t mask;

	__asm volatile ("dsb sy" ::: "memory");

	if (sid == -1)
		mask = DART_ALL_STREAMS(sc);
	else
		mask = (1U << sid);

	HWRITE4(sc, DART_T8020_TLB_SIDMASK, mask);
	HWRITE4(sc, DART_T8020_TLB_CMD, DART_T8020_TLB_CMD_FLUSH);
	while (HREAD4(sc, DART_T8020_TLB_CMD) & DART_T8020_TLB_CMD_BUSY)
		CPU_BUSY_CYCLE();
}

void
apldart_t8110_flush_tlb(struct apldart_softc *sc, int sid)
{
	uint32_t cmd;

	__asm volatile ("dsb sy" ::: "memory");

	if (sid == -1)
		cmd = DART_T8110_TLB_CMD_FLUSH_ALL;
	else
		cmd = DART_T8110_TLB_CMD_FLUSH_SID | sid;

	HWRITE4(sc, DART_T8110_TLB_CMD, cmd);
	while (HREAD4(sc, DART_T8110_TLB_CMD) & DART_T8110_TLB_CMD_BUSY)
		CPU_BUSY_CYCLE();
}

volatile uint64_t *
apldart_lookup_tte(struct apldart_stream *as, bus_addr_t dva)
{
	int idx = (dva & as->as_sc->sc_dvamask) / DART_PAGE_SIZE;
	int l2_idx = idx / (DART_PAGE_SIZE / sizeof(uint64_t));
	int tte_idx = idx % (DART_PAGE_SIZE / sizeof(uint64_t));
	volatile uint64_t *l2;

	l2 = APLDART_DMA_KVA(as->as_l2[l2_idx]);
	return &l2[tte_idx];
}

int
apldart_load_map(struct apldart_stream *as, bus_dmamap_t map, int flags)
{
	struct apldart_softc *sc = as->as_sc;
	struct apldart_map_state *ams = map->_dm_cookie;
	volatile uint64_t *tte;
	int seg, error;

	/* For each segment. */
	for (seg = 0; seg < map->dm_nsegs; seg++) {
		paddr_t pa = map->dm_segs[seg]._ds_paddr;
		psize_t off = pa - apldart_trunc_page(pa);
		psize_t start, end;
		u_long len, dva;

		len = apldart_round_page(map->dm_segs[seg].ds_len + off);

		mtx_enter(&as->as_dvamap_mtx);
		if (flags & BUS_DMA_FIXED) {
			dva = apldart_trunc_page(map->dm_segs[seg].ds_addr);
			/* XXX truncate because "apple,dma-range" mismatch */
			if (dva > sc->sc_dvaend)
				dva &= sc->sc_dvamask;
			error = extent_alloc_region_with_descr(as->as_dvamap,
			    dva, len, EX_NOWAIT, &ams[seg].ams_er);
		} else {
			error = extent_alloc_with_descr(as->as_dvamap, len,
			    DART_PAGE_SIZE, 0, 0, EX_NOWAIT, &ams[seg].ams_er,
			    &dva);
		}
		mtx_leave(&as->as_dvamap_mtx);
		if (error) {
			apldart_unload_map(as, map);
			return error;
		}

		ams[seg].ams_dva = dva;
		ams[seg].ams_len = len;

		map->dm_segs[seg].ds_addr = dva + off;

		pa = apldart_trunc_page(pa);
		start = apldart_trunc_offset(off);
		end = DART_PAGE_MASK;
		while (len > 0) {
			if (len < DART_PAGE_SIZE)
				end = apldart_round_offset(len) - 1;

			tte = apldart_lookup_tte(as, dva);
			*tte = (pa >> sc->sc_shift) | DART_L2_VALID |
			    DART_L2_START(start) | DART_L2_END(end);

			pa += DART_PAGE_SIZE;
			dva += DART_PAGE_SIZE;
			len -= DART_PAGE_SIZE;
			start = 0;
		}
	}

	sc->sc_flush_tlb(sc, as->as_sid);

	return 0;
}

void
apldart_unload_map(struct apldart_stream *as, bus_dmamap_t map)
{
	struct apldart_softc *sc = as->as_sc;
	struct apldart_map_state *ams = map->_dm_cookie;
	volatile uint64_t *tte;
	int seg, error;

	/* For each segment. */
	for (seg = 0; seg < map->dm_nsegs; seg++) {
		u_long len, dva;

		if (ams[seg].ams_len == 0)
			continue;

		dva = ams[seg].ams_dva;
		len = ams[seg].ams_len;

		while (len > 0) {
			tte = apldart_lookup_tte(as, dva);
			*tte = DART_L2_INVAL;

			dva += DART_PAGE_SIZE;
			len -= DART_PAGE_SIZE;
		}

		mtx_enter(&as->as_dvamap_mtx);
		error = extent_free(as->as_dvamap, ams[seg].ams_dva,
		    ams[seg].ams_len, EX_NOWAIT);
		mtx_leave(&as->as_dvamap_mtx);

		KASSERT(error == 0);

		ams[seg].ams_dva = 0;
		ams[seg].ams_len = 0;
	}

	sc->sc_flush_tlb(sc, as->as_sid);
}

int
apldart_dmamap_create(bus_dma_tag_t t, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, int flags, bus_dmamap_t *dmamap)
{
	struct apldart_stream *as = t->_cookie;
	struct apldart_softc *sc = as->as_sc;
	struct apldart_map_state *ams;
	bus_dmamap_t map;
	int error;

	error = sc->sc_dmat->_dmamap_create(sc->sc_dmat, size, nsegments,
	    maxsegsz, boundary, flags, &map);
	if (error)
		return error;

	ams = mallocarray(map->_dm_segcnt, sizeof(*ams), M_DEVBUF,
	    (flags & BUS_DMA_NOWAIT) ? (M_NOWAIT|M_ZERO) : (M_WAITOK|M_ZERO));
	if (ams == NULL) {
		sc->sc_dmat->_dmamap_destroy(sc->sc_dmat, map);
		return ENOMEM;
	}

	map->_dm_cookie = ams;
	*dmamap = map;
	return 0;
}

void
apldart_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{
	struct apldart_stream *as = t->_cookie;
	struct apldart_softc *sc = as->as_sc;
	struct apldart_map_state *ams = map->_dm_cookie;

	if (map->dm_nsegs)
		apldart_dmamap_unload(t, map);

	free(ams, M_DEVBUF, map->_dm_segcnt * sizeof(*ams));
	sc->sc_dmat->_dmamap_destroy(sc->sc_dmat, map);
}

int
apldart_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    size_t buflen, struct proc *p, int flags)
{
	struct apldart_stream *as = t->_cookie;
	struct apldart_softc *sc = as->as_sc;
	int error;

	error = sc->sc_dmat->_dmamap_load(sc->sc_dmat, map,
	    buf, buflen, p, flags);
	if (error)
		return error;

	error = apldart_load_map(as, map, flags);
	if (error)
		sc->sc_dmat->_dmamap_unload(sc->sc_dmat, map);

	return error;
}

int
apldart_dmamap_load_mbuf(bus_dma_tag_t t, bus_dmamap_t map,
    struct mbuf *m, int flags)
{
	struct apldart_stream *as = t->_cookie;
	struct apldart_softc *sc = as->as_sc;
	int error;

	error = sc->sc_dmat->_dmamap_load_mbuf(sc->sc_dmat, map,
	    m, flags);
	if (error)
		return error;

	error = apldart_load_map(as, map, flags);
	if (error)
		sc->sc_dmat->_dmamap_unload(sc->sc_dmat, map);

	return error;
}

int
apldart_dmamap_load_uio(bus_dma_tag_t t, bus_dmamap_t map,
    struct uio *uio, int flags)
{
	struct apldart_stream *as = t->_cookie;
	struct apldart_softc *sc = as->as_sc;
	int error;

	error = sc->sc_dmat->_dmamap_load_uio(sc->sc_dmat, map,
	    uio, flags);
	if (error)
		return error;

	error = apldart_load_map(as, map, flags);
	if (error)
		sc->sc_dmat->_dmamap_unload(sc->sc_dmat, map);

	return error;
}

int
apldart_dmamap_load_raw(bus_dma_tag_t t, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags)
{
	struct apldart_stream *as = t->_cookie;
	struct apldart_softc *sc = as->as_sc;
	int i, error;

	if (flags & BUS_DMA_FIXED) {
		if (map->dm_nsegs != nsegs)
			return EINVAL;
		for (i = 0; i < nsegs; i++) {
			if (map->dm_segs[i].ds_len != segs[i].ds_len)
				return EINVAL;
			map->dm_segs[i]._ds_paddr = segs[i].ds_addr;
			map->dm_segs[i]._ds_vaddr = segs[i]._ds_vaddr;
		}
	} else {
		error = sc->sc_dmat->_dmamap_load_raw(sc->sc_dmat, map,
		     segs, nsegs, size, flags);
		if (error)
			return error;
	}

	error = apldart_load_map(as, map, flags);
	if (error)
		sc->sc_dmat->_dmamap_unload(sc->sc_dmat, map);

	return error;
}

void
apldart_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{
	struct apldart_stream *as = t->_cookie;
	struct apldart_softc *sc = as->as_sc;

	apldart_unload_map(as, map);
	sc->sc_dmat->_dmamap_unload(sc->sc_dmat, map);
}

struct apldart_dmamem *
apldart_dmamem_alloc(bus_dma_tag_t dmat, bus_size_t size, bus_size_t align)
{
	struct apldart_dmamem *adm;
	int nsegs;

	adm = malloc(sizeof(*adm), M_DEVBUF, M_WAITOK | M_ZERO);
	adm->adm_size = size;

	if (bus_dmamap_create(dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &adm->adm_map) != 0)
		goto admfree;

	if (bus_dmamem_alloc(dmat, size, align, 0, &adm->adm_seg, 1,
	    &nsegs, BUS_DMA_WAITOK | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(dmat, &adm->adm_seg, nsegs, size,
	    &adm->adm_kva, BUS_DMA_WAITOK | BUS_DMA_NOCACHE) != 0)
		goto free;

	if (bus_dmamap_load_raw(dmat, adm->adm_map, &adm->adm_seg,
	    nsegs, size, BUS_DMA_WAITOK) != 0)
		goto unmap;

	return adm;

unmap:
	bus_dmamem_unmap(dmat, adm->adm_kva, size);
free:
	bus_dmamem_free(dmat, &adm->adm_seg, 1);
destroy:
	bus_dmamap_destroy(dmat, adm->adm_map);
admfree:
	free(adm, M_DEVBUF, sizeof(*adm));

	return NULL;
}

void
apldart_dmamem_free(bus_dma_tag_t dmat, struct apldart_dmamem *adm)
{
	bus_dmamem_unmap(dmat, adm->adm_kva, adm->adm_size);
	bus_dmamem_free(dmat, &adm->adm_seg, 1);
	bus_dmamap_destroy(dmat, adm->adm_map);
	free(adm, M_DEVBUF, sizeof(*adm));
}
